#include "sd_logger.h"
#include "card_sample.h"
#include "card_display.h"

#include <errno.h>
#include <ff.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static FATFS fatfs;
static struct fs_mount_t mount = {
	.type = FS_FATFS,
	.fs_data = &fatfs,
	.mnt_point = "/SD:",
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static char filename[32];
static char line[512];
static bool mounted;
static bool file_created;
static bool header_synced;
static uint64_t current_boot_id;
static bool rollback_pending;
static off_t rollback_offset;
static int history_cached_index = -1;
static bool history_scan_valid;
static bool history_selected_file;
static bool history_reset_requested;
static struct sd_logger_cursor history_resume;
static struct sd_logger_cursor history_stored_earliest, history_stored_latest;
static int history_first_next_index = -1;
static int history_next_index = -1;
static int history_latest_index = -1;

/* Caller owns storage_bus_lock; all actual CSV mutations invalidate both
 * metadata and row offsets. Image file transactions do not change CSV data.
 */
static void history_cache_invalidate_locked(void)
{
    history_scan_valid = false;
    history_cached_index = -1;
}

#define SD_QUEUE_CAPACITY 640U
static struct card_sample queued[SD_QUEUE_CAPACITY];
static size_t queue_head;
static size_t queue_count;
static bool worker_started;
static bool flush_pending;
static bool utc_restore_pending;
static struct k_thread storage_thread;
K_THREAD_STACK_DEFINE(storage_stack, 4096);
K_MUTEX_DEFINE(queue_lock);
K_MUTEX_DEFINE(storage_bus_lock);
K_SEM_DEFINE(storage_requested, 0, 1);

void sd_logger_bus_lock(void) { k_mutex_lock(&storage_bus_lock, K_FOREVER); }
void sd_logger_bus_unlock(void) { k_mutex_unlock(&storage_bus_lock); }

static void get_sample(const struct card_sample *samples, size_t index,
                       struct card_sample *sample)
{
    if (samples) { *sample = samples[index]; return; }
    k_mutex_lock(&queue_lock, K_FOREVER);
    *sample = queued[(queue_head + index) % SD_QUEUE_CAPACITY];
    k_mutex_unlock(&queue_lock);
}

/* Preserve the sensor's integer units; uptime has no wall-clock meaning. */
static const char csv_header[] =
	"sequence,uptime_s,unix_s,valid_mask,rht_source,temperature_mC,humidity_mpercent,"
	"bme_temperature_mC,bme_humidity_mpercent,pressure_Pa,gas_resistance_ohm,"
	"millilux,co2_ppm,raw_voc,raw_nox,voc_index,nox_index,hcho_millippb,stcc_status,"
	"stcc_self_test,gas_valid,heater_stable,boot_id\n";

static int report_error(const char *operation, int err)
{
	printk("SD LOG: %s failed: %d; file=%s\n", operation, err,
	       filename[0] ? filename : "none");
	return err;
}

static int unmount_card(void)
{
	if (!mounted) {
		return 0;
	}
	int err = fs_unmount(&mount);

	if (err == 0) {
		mounted = false;
	} else {
		report_error("unmount", err);
	}
	return err;
}

static int mount_card(void)
{
	/* A failed unmount leaves Zephyr's mount node linked. Retry it first. */
	int err = unmount_card();

	if (err != 0) {
		return err;
	}
	err = fs_mount(&mount);
	if (err != 0) {
		return report_error("mount", err);
	}
	mounted = true;
	return 0;
}

static int newest_log_index(void);

static int select_filename(void)
{
	struct fs_dirent entry;
	const char *prefix = IS_ENABLED(CONFIG_CARD_TEST_CADENCE) ? "TST" : "LOG";
	int newest = newest_log_index();
	if (newest < -1) { return report_error("find newest filename", newest); }

	/* Deleted old files must not make a new boot reuse an earlier session ID. */
	for (unsigned int index = (unsigned int)(newest + 1); index < 100000; index++) {
		snprintf(filename, sizeof(filename), "/SD:/%s%05u.CSV", prefix, index);
		int err = fs_stat(filename, &entry);

		if (err == -ENOENT) {
			return 0;
		}
		if (err != 0) {
			filename[0] = '\0';
			return report_error("find unused filename", err);
		}
	}
	filename[0] = '\0';
	return report_error("find unused filename", -ENOSPC);
}

static int write_all(struct fs_file_t *file, const char *data, size_t length)
{
	for (size_t offset = 0; offset < length;) {
		ssize_t written = fs_write(file, data + offset, length - offset);

		if (written <= 0) {
			return written < 0 ? (int)written : -ENOSPC;
		}
		offset += written;
	}
	return 0;
}

static int recover_pending(struct fs_file_t *file)
{
	if (!rollback_pending) {
		return 0;
	}
	int err = fs_seek(file, 0, FS_SEEK_END);

	if (err != 0) {
		return report_error("rollback seek", err);
	}
	off_t end = fs_tell(file);

	/* Never expand a damaged/missing prefix when trying to remove a suffix. */
	if (end < 0 || end < rollback_offset) {
		return report_error("rollback length", end < 0 ? (int)end : -EIO);
	}
	err = fs_truncate(file, rollback_offset);
	history_cache_invalidate_locked();
	if (err == 0) {
		err = fs_seek(file, rollback_offset, FS_SEEK_SET);
	}
	if (err == 0) {
		err = fs_sync(file);
	}
	if (err != 0) {
		return report_error("rollback", err);
	}
	rollback_pending = false;
	printk("SD LOG: incomplete batch removed; file=%s\n", filename);
	return 0;
}

static int ensure_header(struct fs_file_t *file)
{
	if (header_synced) {
		return 0;
	}
	int err = fs_seek(file, 0, FS_SEEK_END);

	if (err != 0) {
		return err;
	}
	off_t end = fs_tell(file);

	if (end != 0) {
		return end < 0 ? (int)end : -EIO;
	}
	rollback_offset = 0;
	rollback_pending = true;
	err = write_all(file, csv_header, sizeof(csv_header) - 1);
	if (err == 0) {
		err = fs_sync(file);
	}
	if (err == 0) {
		rollback_pending = false;
		header_synced = true;
		printk("SD LOG: created %s; timestamps are uptime plus optional synchronized UTC\n", filename);
	}
	return err;
}

static int format_sample(const struct card_sample *sample)
{
	const char *source = sample->rht_source == CARD_RHT_SHT45 ? "SHT45" :
			     sample->rht_source == CARD_RHT_BME690 ? "BME690" : "none";
	int length = snprintf(line, sizeof(line), "%u,%u,%llu,0x%04x,%s", sample->sequence,
			      sample->uptime_s, (unsigned long long)sample->unix_s, sample->valid, source);

	if (length < 0 || (size_t)length >= sizeof(line)) {
		return -EOVERFLOW;
	}
	const struct {
		int64_t value;
		uint16_t valid_bit;
	} columns[] = {
		{sample->temperature_mc, CARD_VALID_TEMPERATURE},
		{sample->humidity_mpercent, CARD_VALID_HUMIDITY},
		{sample->bme_temperature_mc, CARD_VALID_BME_TEMPERATURE},
		{sample->bme_humidity_mpercent, CARD_VALID_BME_HUMIDITY},
		{sample->pressure_pa, CARD_VALID_PRESSURE},
		{sample->gas_resistance_ohm, CARD_VALID_BME_GAS},
		{sample->millilux, CARD_VALID_LUX},
		{sample->co2_ppm, CARD_VALID_CO2},
		{sample->raw_voc, CARD_VALID_VOC_RAW},
		{sample->raw_nox, CARD_VALID_NOX_RAW},
		{sample->voc_index, CARD_VALID_VOC_INDEX},
		{sample->nox_index, CARD_VALID_NOX_INDEX},
		{sample->hcho_millippb, CARD_VALID_HCHO},
		/* Diagnostic fields have no individual validity bit. */
		{sample->stcc_status, 0},
		{sample->stcc_self_test, 0},
		{sample->gas_valid, 0},
		{sample->heater_stable, 0},
	};

	for (size_t i = 0; i < ARRAY_SIZE(columns); i++) {
		bool valid = columns[i].valid_bit == 0 ||
			     (sample->valid & columns[i].valid_bit) != 0;
		int added = valid ? snprintf(line + length, sizeof(line) - length,
					    ",%lld", (long long)columns[i].value) :
				    snprintf(line + length, sizeof(line) - length, ",");

		if (added < 0 || (size_t)added >= sizeof(line) - (size_t)length) {
			return -EOVERFLOW;
		}
		length += added;
	}
	int added = snprintf(line + length, sizeof(line) - (size_t)length,
			     ",%llu\n", (unsigned long long)current_boot_id);
	return added < 0 || (size_t)added >= sizeof(line) - (size_t)length ?
		-EOVERFLOW : length + added;
}

#ifdef CONFIG_CARD_TEST_CADENCE
static int verify_text(struct fs_file_t *file, const char *expected, size_t length)
{
	char received[64];

	for (size_t offset = 0; offset < length;) {
		size_t requested = MIN(sizeof(received), length - offset);
		ssize_t count = fs_read(file, received, requested);

		if (count <= 0) {
			return count < 0 ? (int)count : -EIO;
		}
		if ((size_t)count > requested ||
		    memcmp(received, expected + offset, (size_t)count) != 0) {
			return -EILSEQ;
		}
		offset += (size_t)count;
	}
	return 0;
}

/* Read through a fresh mount after the write handle and mount were closed. */
static int verify_recent_batch(const struct card_sample *samples, size_t count,
			       off_t batch_start, size_t batch_bytes)
{
	int err = mount_card();

	if (err != 0) {
		return err;
	}
	struct fs_file_t file;
	bool opened = false;

	fs_file_t_init(&file);
	err = fs_open(&file, filename, FS_O_READ);
	opened = err == 0;
	if (err == 0) {
		err = verify_text(&file, csv_header, sizeof(csv_header) - 1);
	}
	if (err == 0) {
		err = fs_seek(&file, batch_start, FS_SEEK_SET);
	}
	for (size_t i = 0; err == 0 && i < count; i++) {
		struct card_sample sample;
        get_sample(samples, i, &sample);
        int length = format_sample(&sample);

		err = length < 0 ? length : verify_text(&file, line, (size_t)length);
	}
	if (err == 0) {
		/* The compared batch must be the whole file tail, with no extra rows. */
		err = fs_seek(&file, 0, FS_SEEK_END);
		if (err == 0) {
			off_t end = fs_tell(&file);

			if (end < 0) {
				err = (int)end;
			} else if ((uint64_t)end != (uint64_t)batch_start + batch_bytes) {
				err = -EILSEQ;
			}
		}
	}
	if (opened) {
		int close_err = fs_close(&file);

		fs_file_t_init(&file);
		if (err == 0) {
			err = close_err;
		}
	}
	int unmount_err = unmount_card();

	if (err == 0) {
		err = unmount_err;
	}
	if (err != 0) {
		return report_error("verify after remount", err);
	}
	printk("SD LOG: VERIFY PASS rows=%u verified_rows=%u bytes=%u; file=%s\n",
	       (unsigned int)count, (unsigned int)count,
	       (unsigned int)batch_bytes, filename);
	return 0;
}
#endif

static int append_batch(const struct card_sample *samples, size_t count, size_t *committed)
{
	if (committed == NULL) {
		return -EINVAL;
	}
	*committed = 0;
	if (count == 0) {
		return 0;
	}
	history_cache_invalidate_locked();
	int err = mount_card();

	if (err != 0) {
		return err;
	}
	struct fs_file_t file;
	bool opened = false;
#ifdef CONFIG_CARD_TEST_CADENCE
	off_t batch_start = 0;
	size_t batch_bytes = 0;
#endif

	fs_file_t_init(&file);
	if (filename[0] == '\0') {
		err = select_filename();
	}
	if (err == 0) {
		err = fs_open(&file, filename, FS_O_WRITE | (file_created ? 0 : FS_O_CREATE));
		opened = err == 0;
		file_created |= opened;
		if (err != 0) {
			report_error("open", err);
		}
	}
	if (err == 0) {
		err = recover_pending(&file);
	}
	if (err == 0) {
		err = ensure_header(&file);
		if (err != 0) {
			report_error("header", err);
		}
	}
	if (err == 0) {
		err = fs_seek(&file, 0, FS_SEEK_END);
		if (err == 0) {
			rollback_offset = fs_tell(&file);
			if (rollback_offset < 0) {
				err = (int)rollback_offset;
			} else {
				rollback_pending = true;
#ifdef CONFIG_CARD_TEST_CADENCE
				batch_start = rollback_offset;
#endif
			}
		}
	}
	for (size_t i = 0; err == 0 && i < count; i++) {
		struct card_sample sample;
        get_sample(samples, i, &sample);
        int length = format_sample(&sample);

		err = length < 0 ? length : write_all(&file, line, length);
#ifdef CONFIG_CARD_TEST_CADENCE
		if (err == 0) {
			batch_bytes += (size_t)length;
		}
#endif
	}
	if (err == 0) {
		err = fs_sync(&file);
		if (err == 0) {
			rollback_pending = false;
			*committed = count;
			printk("SD LOG: synced %u rows; file=%s\n", (unsigned int)count, filename);
		}
	}
	if (opened) {
		if (err != 0) {
			report_error("append", err);
			/* A media error can make this handle unusable. Keep recovery state
			 * if rollback fails, and retry after reopening on the next call.
			 */
			(void)recover_pending(&file);
		}
		int close_err = fs_close(&file);

		/* FatFS releases its FIL even if close reports an error. */
		fs_file_t_init(&file);
		if (close_err != 0) {
			report_error("close", close_err);
			if (err == 0) {
				err = close_err;
			}
		}
	}
	int unmount_err = unmount_card();

#ifdef CONFIG_CARD_TEST_CADENCE
	if (err == 0 && unmount_err == 0 && *committed == count) {
		/* These rows remain committed even when readback reports an error. */
		err = verify_recent_batch(samples, count, batch_start, batch_bytes);
	}
#endif
	return err != 0 ? err : unmount_err;
}

int sd_logger_append(const struct card_sample *samples, size_t count, size_t *committed)
{
    if (!samples && count) { return -EINVAL; }
    sd_logger_bus_lock();
    int result = append_batch(samples, count, committed);
    sd_logger_bus_unlock();
    return result;
}

/* Header-based reader accepts both the original minute CSV and the 1Hz CSV.
 * No fabricated timestamps: only explicit unix_s permits cross-boot merging. */
enum csv_column {
    COL_SEQUENCE, COL_UPTIME, COL_UNIX, COL_VALID, COL_RHT,
    COL_TEMP, COL_HUM, COL_BME_TEMP, COL_BME_HUM, COL_PRESS, COL_GAS,
    COL_LUX, COL_CO2, COL_RAW_VOC, COL_RAW_NOX, COL_VOC, COL_NOX, COL_HCHO,
    COL_STCC_STATUS, COL_SELF_TEST, COL_GAS_VALID, COL_HEATER, COL_BOOT, COL_COUNT
};
static const char *const column_names[COL_COUNT] = {
    "sequence", "uptime_s", "unix_s", "valid_mask", "rht_source",
    "temperature_mC", "humidity_mpercent", "bme_temperature_mC", "bme_humidity_mpercent",
    "pressure_Pa", "gas_resistance_ohm", "millilux", "co2_ppm", "raw_voc", "raw_nox",
    "voc_index", "nox_index", "hcho_millippb", "stcc_status", "stcc_self_test",
    "gas_valid", "heater_stable", "boot_id"
};
struct csv_schema { int8_t index[COL_COUNT]; uint8_t count; };
struct csv_reader {
    struct fs_file_t *file;
    char bytes[512];
    size_t next, length;
    off_t position;
};

static int reader_seek(struct csv_reader *reader, off_t offset)
{
    reader->next = reader->length = 0;
    reader->position = offset;
    return fs_seek(reader->file, offset, FS_SEEK_SET);
}

/* Unterminated rows at power-loss EOF are not accepted as committed readings. */
static int reader_line(struct csv_reader *reader, char *buffer, size_t capacity)
{
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        if (reader->next == reader->length) {
            ssize_t count = fs_read(reader->file, reader->bytes, sizeof(reader->bytes));
            if (count < 0) { return (int)count; }
            if (!count) { return 0; }
            reader->next = 0;
            reader->length = (size_t)count;
        }
        char ch = reader->bytes[reader->next++];
        ++reader->position;
        if (ch == '\n') {
            if (overflow) { return -EOVERFLOW; }
            if (length && buffer[length - 1] == '\r') { --length; }
            buffer[length] = '\0';
            return 1;
        }
        if (length + 1 < capacity) { buffer[length++] = ch; }
        else { overflow = true; }
    }
}

static size_t split_columns(char *text, char **columns, size_t capacity)
{
    size_t count = 0;
    columns[count++] = text;
    for (char *p = text; *p; ++p) {
        if (*p == ',') {
            if (count == capacity) { return 0; }
            *p = '\0'; columns[count++] = p + 1;
        }
    }
    return count;
}

static int parse_header(char *text, struct csv_schema *schema)
{
    char *columns[COL_COUNT + 4];
    size_t count = split_columns(text, columns, ARRAY_SIZE(columns));
    if (!count) { return -EINVAL; }
    memset(schema->index, -1, sizeof(schema->index));
    schema->count = count;
    for (size_t i = 0; i < count; ++i) {
        for (size_t field = 0; field < COL_COUNT; ++field) {
            if (!strcmp(columns[i], column_names[field])) {
                if (schema->index[field] >= 0) { return -EINVAL; }
                schema->index[field] = i;
            }
        }
    }
    return schema->index[COL_SEQUENCE] < 0 || schema->index[COL_UPTIME] < 0 ||
        schema->index[COL_VALID] < 0 ? -EINVAL : 0;
}

static bool parse_number(const char *text, int base, int64_t low, int64_t high, int64_t *value)
{
    if (!text || !text[0]) { return false; }
    errno = 0;
    char *end;
    long long parsed = strtoll(text, &end, base);
    if (errno || *end || parsed < low || parsed > high) { return false; }
    *value = parsed;
    return true;
}

static int parse_record(char *text, const struct csv_schema *schema,
                         struct card_sample *sample, uint64_t *boot_id)
{
    char *columns[COL_COUNT + 4];
    size_t count = split_columns(text, columns, ARRAY_SIZE(columns));
    if (count != schema->count) { return -EINVAL; }
    memset(sample, 0, sizeof(*sample));
    if (boot_id) { *boot_id = 0; }
    int64_t value;
    if (!parse_number(columns[schema->index[COL_SEQUENCE]], 10, 0, UINT32_MAX, &value)) { return -EINVAL; }
    sample->sequence = value;
    if (!parse_number(columns[schema->index[COL_UPTIME]], 10, 0, UINT32_MAX, &value)) { return -EINVAL; }
    sample->uptime_s = value;
    if (!parse_number(columns[schema->index[COL_VALID]], 0, 0, UINT16_MAX, &value)) { return -EINVAL; }
    sample->valid = value;
    if (schema->index[COL_BOOT] >= 0) {
        const char *identity = columns[schema->index[COL_BOOT]];
        if (!parse_number(identity, 10, 1, INT64_MAX, &value)) { return -EINVAL; }
        if (boot_id) { *boot_id = (uint64_t)value; }
    }
    if (schema->index[COL_UNIX] >= 0) {
        const char *unix_text = columns[schema->index[COL_UNIX]];
        if (unix_text[0] && !parse_number(unix_text, 10, 0, 4102444799LL, &value)) { return -EINVAL; }
        if (unix_text[0]) {
            if (value && value < 1577836800LL) { return -EINVAL; }
            sample->unix_s = value;
        }
    }
    if (schema->index[COL_RHT] >= 0) {
        const char *source = columns[schema->index[COL_RHT]];
        sample->rht_source = !strcmp(source, "SHT45") ? CARD_RHT_SHT45 :
            !strcmp(source, "BME690") ? CARD_RHT_BME690 : CARD_RHT_NONE;
    }
#define READ_FIELD(column, member, minimum, maximum, bit) do { \
    int field_index = schema->index[column]; \
    if (field_index >= 0 && parse_number(columns[field_index], 10, minimum, maximum, &value)) { \
        sample->member = value; \
    } else { sample->valid &= (uint16_t)~(bit); } \
} while (0)
    READ_FIELD(COL_TEMP, temperature_mc, INT32_MIN, INT32_MAX, CARD_VALID_TEMPERATURE);
    READ_FIELD(COL_HUM, humidity_mpercent, 0, UINT32_MAX, CARD_VALID_HUMIDITY);
    READ_FIELD(COL_BME_TEMP, bme_temperature_mc, INT32_MIN, INT32_MAX, CARD_VALID_BME_TEMPERATURE);
    READ_FIELD(COL_BME_HUM, bme_humidity_mpercent, 0, UINT32_MAX, CARD_VALID_BME_HUMIDITY);
    READ_FIELD(COL_PRESS, pressure_pa, 0, UINT32_MAX, CARD_VALID_PRESSURE);
    READ_FIELD(COL_GAS, gas_resistance_ohm, 0, UINT32_MAX, CARD_VALID_BME_GAS);
    READ_FIELD(COL_LUX, millilux, 0, UINT32_MAX, CARD_VALID_LUX);
    READ_FIELD(COL_CO2, co2_ppm, 0, INT16_MAX, CARD_VALID_CO2);
    READ_FIELD(COL_RAW_VOC, raw_voc, 0, UINT16_MAX, CARD_VALID_VOC_RAW);
    READ_FIELD(COL_RAW_NOX, raw_nox, 0, UINT16_MAX, CARD_VALID_NOX_RAW);
    READ_FIELD(COL_VOC, voc_index, 0, UINT16_MAX, CARD_VALID_VOC_INDEX);
    READ_FIELD(COL_NOX, nox_index, 0, UINT16_MAX, CARD_VALID_NOX_INDEX);
    READ_FIELD(COL_HCHO, hcho_millippb, INT32_MIN, INT32_MAX, CARD_VALID_HCHO);
    READ_FIELD(COL_STCC_STATUS, stcc_status, 0, UINT16_MAX, 0);
    READ_FIELD(COL_SELF_TEST, stcc_self_test, 0, UINT16_MAX, 0);
    READ_FIELD(COL_GAS_VALID, gas_valid, 0, UINT8_MAX, 0);
    READ_FIELD(COL_HEATER, heater_stable, 0, UINT8_MAX, 0);
#undef READ_FIELD
    return 0;
}

static int parse_row(char *text, const struct csv_schema *schema, struct card_sample *sample)
{
    return parse_record(text, schema, sample, NULL);
}

static const char *log_prefix(void)
{
    return IS_ENABLED(CONFIG_CARD_TEST_CADENCE) ? "TST" : "LOG";
}

static int newest_log_index(void)
{
    struct fs_dir_t directory;
    struct fs_dirent entry;
    fs_dir_t_init(&directory);
    int error = fs_opendir(&directory, "/SD:");
    if (error) { return error; }
    int newest = -1;
    while (!(error = fs_readdir(&directory, &entry)) && entry.name[0]) {
        if (entry.type != FS_DIR_ENTRY_FILE || strlen(entry.name) != 12 ||
            strncmp(entry.name, log_prefix(), 3) || strcmp(entry.name + 8, ".CSV")) { continue; }
        int index = 0;
        bool digits = true;
        for (size_t i = 3; i < 8; ++i) {
            if (entry.name[i] < '0' || entry.name[i] > '9') { digits = false; break; }
            index = index * 10 + entry.name[i] - '0';
        }
        if (digits && index > newest) { newest = index; }
    }
    int close_error = fs_closedir(&directory);
    return error ? error : close_error ? close_error : newest;
}

/* Locate the final complete valid row in a bounded tail, regardless of an
 * interrupted append suffix. A CSV row is at most sizeof(line)-1 bytes. */
static int read_log_tail(struct csv_reader *reader, const struct csv_schema *schema,
                         off_t end, struct card_sample *last)
{
    off_t start = end > 4096 ? end - 4096 : 0;
    int error = reader_seek(reader, start);
    if (error) { return error; }
    if (start) { (void)reader_line(reader, line, sizeof(line)); }
    bool found = false;
    int result;
    while ((result = reader_line(reader, line, sizeof(line))) != 0) {
        if (result < 0) { if (result == -EOVERFLOW) { continue; } return result; }
        struct card_sample sample;
        if (!parse_row(line, schema, &sample)) { *last = sample; found = true; }
    }
    return found ? 0 : -ENODATA;
}

/* Binary-search by monotonic uptime. Only about log2(file bytes) line reads
 * precede the streaming 24h tail, so old multi-day files need not be replayed. */
static int seek_history_start(struct fs_file_t *file, const struct csv_schema *schema,
                              off_t data_start, off_t end, uint32_t cutoff, bool by_sequence,
                              off_t *offset)
{
    off_t low = data_start, high = end;
    while (high - low > 2048) {
        off_t middle = low + (high - low) / 2;
        struct csv_reader reader = {.file = file};
        int error = reader_seek(&reader, middle);
        if (error) { return error; }
        int result = reader_line(&reader, line, sizeof(line));
        if (result < 0 && result != -EOVERFLOW) { return result; }
        struct card_sample sample;
        bool found = false;
        for (int attempts = 0; attempts < 8; ++attempts) {
            result = reader_line(&reader, line, sizeof(line));
            if (result <= 0) { break; }
            if (!parse_row(line, schema, &sample)) { found = true; break; }
        }
        uint32_t key = found ? (by_sequence ? sample.sequence : sample.uptime_s) : 0;
        if (!found || key >= cutoff) { high = middle; }
        else { low = reader.position; }
    }
    /* Starting earlier is safe; restore filters exact timestamps itself. */
    *offset = low;
    return 0;
}

static int restore_log(const char *path, bool require_utc, bool *started_restore, bool *utc_axis,
                        uint64_t *anchor, size_t *rows)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int error = fs_open(&file, path, FS_O_READ);
    if (error) { return error; }
    struct csv_reader reader = {.file = &file};
    struct csv_schema schema;
    off_t data_start = 0, end = 0;
    struct card_sample last;
    int result = reader_line(&reader, line, sizeof(line));
    error = result <= 0 ? -ENODATA : parse_header(line, &schema);
    if (!error) {
        data_start = reader.position;
        error = fs_seek(&file, 0, FS_SEEK_END);
    }
    if (!error) {
        end = fs_tell(&file);
        error = end < 0 ? (int)end : read_log_tail(&reader, &schema, end, &last);
    }
    if (!error && require_utc && !last.unix_s) { error = -ENODATA; }
    if (!error && !*started_restore) {
        *utc_axis = last.unix_s != 0;
        *anchor = *utc_axis ? last.unix_s : last.uptime_s;
        card_display_saved_begin(*utc_axis, *anchor);
        *started_restore = true;
    }
    if (!error && *utc_axis && !last.unix_s) { error = -ENODATA; }
    if (!error) {
        uint64_t cutoff = *anchor >= 86400 ? *anchor - 86400 : 0;
        uint32_t uptime_cutoff = 0;
        if (!*utc_axis) { uptime_cutoff = (uint32_t)cutoff; }
        else if (last.unix_s > cutoff && last.unix_s <= *anchor) {
            uint64_t span = last.unix_s - cutoff;
            uptime_cutoff = last.uptime_s > span ? last.uptime_s - span : 0;
        } else if (last.unix_s < cutoff) { error = -ERANGE; }
        off_t offset = data_start;
        if (!error) { error = seek_history_start(&file, &schema, data_start, end, uptime_cutoff, false, &offset); }
        if (!error) { error = reader_seek(&reader, offset); }
        /* The low bound is always a complete row boundary. */
        while (!error && (result = reader_line(&reader, line, sizeof(line))) != 0) {
            if (result < 0) {
                if (result == -EOVERFLOW) { continue; }
                error = result; break;
            }
            struct card_sample sample;
            if (parse_row(line, &schema, &sample)) { continue; }
            if (schema.index[COL_BOOT] >= 0 && last.unix_s) {
                int64_t resolved = (int64_t)last.unix_s + sample.uptime_s - last.uptime_s;
                sample.unix_s = resolved >= 1577836800LL && resolved < 4102444800LL ?
                    (uint64_t)resolved : 0;
            }
            uint64_t stamp = *utc_axis ? sample.unix_s : sample.uptime_s;
            if ((!*utc_axis || sample.unix_s) && stamp >= cutoff && stamp <= *anchor) {
                card_display_saved_record(&sample); ++*rows;
            }
        }
    }
    int close_error = fs_close(&file);
    return error ? error : close_error;
}

static void restore_history(bool require_utc)
{
    sd_logger_bus_lock();
    int error = mount_card();
    int newest = error ? -1 : newest_log_index();
    bool begun = false, utc_axis = false;
    uint64_t anchor = 0;
    size_t rows = 0;
    char path[32];
    if (!error && newest < -1) { error = newest; }
    for (int index = newest; !error && index >= 0; --index) {
        snprintf(path, sizeof(path), "/SD:/%s%05u.CSV", log_prefix(), (unsigned int)index);
        int restored = restore_log(path, require_utc, &begun, &utc_axis, &anchor, &rows);
        if (begun && !utc_axis) { break; } /* Unknown outage: exactly one boot. */
        if (restored && restored != -ENOENT && restored != -ENODATA &&
            restored != -EINVAL && restored != -ERANGE) { error = restored; }
        /* An older UTC file can be out of order after a clock correction;
         * don't stop scanning solely because its final timestamp is old. */
    }
    int unmount_error = unmount_card();
    sd_logger_bus_unlock();
    if (begun) { card_display_saved_end(); }
    if (require_utc && !begun) {
        printk("SD HISTORY: no UTC archive; saved history unchanged; error=%d\n",
               error ? error : unmount_error);
        return;
    }
    printk("SD HISTORY: restore rows=%u axis=%s end=%llu error=%d\n", (unsigned int)rows,
           utc_axis ? "UTC" : "single saved session (clock unknown)",
           (unsigned long long)anchor, error ? error : unmount_error);
}

static void flush_queue(void)
{
    k_mutex_lock(&queue_lock, K_FOREVER);
    size_t count = MIN(queue_count, 600U);
    k_mutex_unlock(&queue_lock);
    if (!count) { return; }
    size_t committed = 0;
    sd_logger_bus_lock();
    int error = append_batch(NULL, count, &committed);
    sd_logger_bus_unlock();
    k_mutex_lock(&queue_lock, K_FOREVER);
    if (committed <= queue_count) {
        queue_head = (queue_head + committed) % SD_QUEUE_CAPACITY;
        queue_count -= committed;
    }
    size_t remaining = queue_count;
    k_mutex_unlock(&queue_lock);
    if (error) { printk("ERROR: SD worker batch=%d committed=%u pending=%u\n", error,
                       (unsigned int)committed, (unsigned int)remaining); }
}

static void storage_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    restore_history(false);
    for (;;) {
        k_sem_take(&storage_requested, K_FOREVER);
        k_mutex_lock(&queue_lock, K_FOREVER);
        bool restore_utc = utc_restore_pending;
        bool flush = flush_pending;
        utc_restore_pending = false;
        flush_pending = false;
        k_mutex_unlock(&queue_lock);
        if (restore_utc) { restore_history(true); }
        if (flush) { flush_queue(); }
    }
}

int sd_logger_init(void)
{
    k_mutex_lock(&queue_lock, K_FOREVER);
    if (worker_started) { k_mutex_unlock(&queue_lock); return -EALREADY; }
    if (!current_boot_id) { k_mutex_unlock(&queue_lock); return -EINVAL; }
    worker_started = true;
    k_thread_create(&storage_thread, storage_stack, K_THREAD_STACK_SIZEOF(storage_stack),
                    storage_worker, NULL, NULL, NULL, 9, 0, K_NO_WAIT);
    k_mutex_unlock(&queue_lock);
    return 0;
}

int sd_logger_set_boot_id(uint64_t boot_id)
{
    if (!boot_id || boot_id > INT64_MAX) { return -EINVAL; }
    k_mutex_lock(&queue_lock, K_FOREVER);
    int error = worker_started ? -EBUSY : 0;
    if (!error) { current_boot_id = boot_id; }
    k_mutex_unlock(&queue_lock);
    return error;
}

int sd_logger_queue(const struct card_sample *sample)
{
    if (!sample) { return -EINVAL; }
    k_mutex_lock(&queue_lock, K_FOREVER);
    int error = !worker_started ? -EACCES : queue_count == SD_QUEUE_CAPACITY ? -ENOSPC : 0;
    if (!error) {
        queued[(queue_head + queue_count) % SD_QUEUE_CAPACITY] = *sample;
        ++queue_count;
    }
    k_mutex_unlock(&queue_lock);
    return error;
}

int sd_logger_flush_async(void)
{
    k_mutex_lock(&queue_lock, K_FOREVER);
    int error = !worker_started ? -EACCES : flush_pending ? -EALREADY : 0;
    if (!error) { flush_pending = true; }
    k_mutex_unlock(&queue_lock);
    if (!error) { k_sem_give(&storage_requested); }
    return error;
}

int sd_logger_restore_utc_async(void)
{
    k_mutex_lock(&queue_lock, K_FOREVER);
    int error = !worker_started ? -EACCES : utc_restore_pending ? -EALREADY : 0;
    if (!error) { utc_restore_pending = true; }
    k_mutex_unlock(&queue_lock);
    if (!error) { k_sem_give(&storage_requested); }
    return error;
}

int sd_logger_storage_begin(void)
{
    sd_logger_bus_lock();
    int error = mount_card();
    if (error) { sd_logger_bus_unlock(); }
    return error;
}

int sd_logger_storage_end(void)
{
    int error = unmount_card();
    sd_logger_bus_unlock();
    return error;
}

/* Small cursor cache accelerates sequential pages without retaining an open
 * file/mount across BLE waits. The shared bus lock protects it and CSV scratch.
 */
static struct sd_logger_cursor history_cached;
static off_t history_cached_offset;
struct history_file {
    struct csv_schema schema;
    uint64_t boot_id;
    struct card_sample first, last;
    off_t data_start, end;
    int index;
};
/* Shared storage scratch is protected by storage_bus_lock; it avoids retaining
 * four file metadata structures on the history worker's bounded call stack.
 */
static struct {
    struct history_file first, selected, info, next;
} history_workspace;

static int log_name_index(const char *name)
{
    if (strlen(name) != 12 || strncmp(name, log_prefix(), 3) ||
        strcmp(name + 8, ".CSV")) { return -1; }
    int index = 0;
    for (size_t i = 3; i < 8; ++i) {
        if (name[i] < '0' || name[i] > '9') { return -1; }
        index = index * 10 + name[i] - '0';
    }
    return index;
}

static int next_log_index(int after)
{
    struct fs_dir_t directory;
    struct fs_dirent entry;
    fs_dir_t_init(&directory);
    int error = fs_opendir(&directory, "/SD:");
    if (error) { return error; }
    int next = -1;
    while (!(error = fs_readdir(&directory, &entry)) && entry.name[0]) {
        if (entry.type != FS_DIR_ENTRY_FILE) { continue; }
        int index = log_name_index(entry.name);
        if (index > after && (next < 0 || index < next)) { next = index; }
    }
    int close_error = fs_closedir(&directory);
    return error ? error : close_error ? close_error : next;
}

static void history_path(char *path, size_t length, int index)
{
    snprintf(path, length, "/SD:/%s%05u.CSV", log_prefix(), (unsigned int)index);
}

static uint64_t legacy_boot_id(int index)
{
    /* Reserved namespace: stable per legacy filename, scoped by device ID in
     * the receiver. It makes no claim about UTC or time between old boots.
     */
    return UINT64_C(0x8000000000000000) |
        ((IS_ENABLED(CONFIG_CARD_TEST_CADENCE) ? UINT64_C(2) : UINT64_C(1)) << 32) |
        (uint64_t)(unsigned int)index;
}

static int history_file_info(int index, struct history_file *info)
{
    char path[32]; history_path(path, sizeof(path), index);
    struct fs_file_t file; fs_file_t_init(&file);
    int error = fs_open(&file, path, FS_O_READ);
    if (error) { return error; }
    memset(info, 0, sizeof(*info)); info->index = index;
    struct csv_reader reader = {.file = &file};
    int result = reader_line(&reader, line, sizeof(line));
    error = result <= 0 ? -ENODATA : parse_header(line, &info->schema);
    if (!error) {
        info->data_start = reader.position;
        error = -ENODATA;
        while ((result = reader_line(&reader, line, sizeof(line))) > 0) {
            if (!parse_record(line, &info->schema, &info->first, &info->boot_id)) {
                error = 0; break;
            }
        }
        if (result < 0) { error = result; }
    }
    if (!error) { error = fs_seek(&file, 0, FS_SEEK_END); }
    if (!error) {
        info->end = fs_tell(&file);
        error = info->end < 0 ? (int)info->end :
            read_log_tail(&reader, &info->schema, info->end, &info->last);
    }
    if (!error && !info->boot_id) { info->boot_id = legacy_boot_id(index); }
    int closed = fs_close(&file);
    return error ? error : closed;
}

static int next_history_file(int after, struct history_file *info)
{
    for (;;) {
        int index = next_log_index(after);
        if (index < 0) { return index == -1 ? -ENODATA : index; }
        int error = history_file_info(index, info);
        if (!error) { return 0; }
        if (error != -ENODATA && error != -EINVAL && error != -ENOENT) { return error; }
        after = index; /* Empty/incomplete legacy file has no retained rows. */
    }
}

static bool cursor_equal(struct sd_logger_cursor left, struct sd_logger_cursor right)
{
    return left.boot_id == right.boot_id && left.sequence == right.sequence;
}

static int history_cursor_present(const struct history_file *info, uint32_t sequence)
{
    if (sequence < info->first.sequence || sequence > info->last.sequence) { return 0; }
    struct sd_logger_cursor cursor = {info->boot_id, sequence};
    if (history_cached_index == info->index && cursor_equal(cursor, history_cached)) { return 1; }
    char path[32]; history_path(path, sizeof(path), info->index);
    struct fs_file_t file; fs_file_t_init(&file);
    int error = fs_open(&file, path, FS_O_READ);
    if (error) { return error; }
    off_t offset = info->data_start;
    error = seek_history_start(&file, &info->schema, info->data_start,
                               info->end, sequence, true, &offset);
    struct csv_reader reader = {.file = &file};
    if (!error) { error = reader_seek(&reader, offset); }
    int present = 0, result;
    while (!error && (result = reader_line(&reader, line, sizeof(line))) != 0) {
        if (result < 0) { error = result; break; }
        struct card_sample sample;
        if (parse_row(line, &info->schema, &sample)) { continue; }
        if (sample.sequence >= sequence) { present = sample.sequence == sequence; break; }
    }
    int closed = fs_close(&file);
    return error ? error : closed ? closed : present;
}

static int history_file_rows(const struct history_file *info, uint32_t after,
                             struct card_sample *samples, size_t capacity,
                             struct sd_logger_history_page *page)
{
    char path[32]; history_path(path, sizeof(path), info->index);
    struct fs_file_t file; fs_file_t_init(&file);
    int error = fs_open(&file, path, FS_O_READ);
    if (error) { return error; }
    struct csv_reader reader = {.file = &file};
    struct sd_logger_cursor cursor = {info->boot_id, after};
    off_t offset = info->data_start;
    if (history_cached_index == info->index && history_cached_offset <= info->end &&
        cursor_equal(cursor, history_cached)) {
        offset = history_cached_offset;
    } else if (after && after < UINT32_MAX) {
        error = seek_history_start(&file, &info->schema, info->data_start,
                                   info->end, after + 1U, true, &offset);
    }
    if (!error) { error = reader_seek(&reader, offset); }
    int result = 0;
    uint32_t previous = after;
    while (!error && page->count < capacity &&
           (result = reader_line(&reader, line, sizeof(line))) != 0) {
        if (result < 0) { error = result; break; }
        struct card_sample sample;
        uint64_t identity;
        if (parse_record(line, &info->schema, &sample, &identity)) { continue; }
        if (identity && identity != info->boot_id) { error = -EBADMSG; break; }
        if (sample.sequence <= after) { continue; }
        if (previous && sample.sequence != previous + 1U) { page->gap = true; }
        /* A saved anchor from this exact boot resolves its early unknown rows.
         * Completely clock-unknown old files remain unknown.
         */
        if (info->last.unix_s && (info->schema.index[COL_BOOT] >= 0 || !sample.unix_s)) {
            int64_t resolved = (int64_t)info->last.unix_s + sample.uptime_s - info->last.uptime_s;
            sample.unix_s = resolved >= 1577836800LL && resolved < 4102444800LL ? (uint64_t)resolved : 0;
        }
        samples[page->count++] = sample;
        previous = sample.sequence;
        history_cached = (struct sd_logger_cursor){info->boot_id, sample.sequence};
        history_cached_index = info->index;
        history_cached_offset = reader.position;
    }
    int closed = fs_close(&file);
    return error ? error : closed;
}

int sd_logger_history_read(struct sd_logger_cursor after,
                          struct card_sample *samples, size_t capacity,
                          struct sd_logger_history_page *page)
{
    if (!samples || !page || !capacity || capacity > 6 ||
        (!after.boot_id != !after.sequence)) { return -EINVAL; }
    memset(page, 0, sizeof(*page));
    int error = sd_logger_storage_begin();
    if (error) { return error; }
    struct history_file *first = &history_workspace.first;
    struct history_file *selected = &history_workspace.selected;
    struct history_file *info = &history_workspace.info;
    struct history_file *next = &history_workspace.next;
    k_mutex_lock(&queue_lock, K_FOREVER);
    bool reset = history_reset_requested; history_reset_requested = false;
    k_mutex_unlock(&queue_lock);
    if (reset) { history_cache_invalidate_locked(); }
    bool resume = history_scan_valid && cursor_equal(after, history_resume);
    bool reuse = history_scan_valid && (!after.boot_id || resume ||
        (history_selected_file && after.boot_id == selected->boot_id) || after.boot_id == current_boot_id);
    int first_error = history_stored_earliest.boot_id ? 0 : -ENODATA;
    bool selected_file = false, cursor_found = !after.boot_id || resume;
    if (reuse) {
        page->earliest = history_stored_earliest;
        page->latest = history_stored_latest;
        selected_file = history_selected_file;
        if (!after.boot_id) {
            selected_file = !first_error;
            if (selected_file) { *selected = *first; history_next_index = history_first_next_index; }
        } else if (after.boot_id == current_boot_id &&
                   (!selected_file || selected->boot_id != current_boot_id)) {
            selected_file = history_stored_latest.boot_id == current_boot_id;
            if (selected_file) { error = history_file_info(history_latest_index, selected); }
            history_next_index = -1;
        }
    } else {
        first_error = next_history_file(-1, first);
        if (first_error && first_error != -ENODATA) { error = first_error; }
        history_stored_earliest = history_stored_latest = (struct sd_logger_cursor){0};
        history_first_next_index = history_next_index = history_latest_index = -1;
        if (!first_error) {
            page->earliest = (struct sd_logger_cursor){first->boot_id, first->first.sequence};
            *info = *first;
            for (;;) {
                page->latest = (struct sd_logger_cursor){info->boot_id, info->last.sequence};
                history_latest_index = info->index;
                if (!selected_file && (!after.boot_id || info->boot_id == after.boot_id)) {
                    *selected = *info; selected_file = true; cursor_found = true;
                }
                int result = next_history_file(info->index, next);
                if (result == -ENODATA) { break; }
                if (result) { error = result; break; }
                if (info->index == first->index) { history_first_next_index = next->index; }
                if (selected_file && info->index == selected->index) { history_next_index = next->index; }
                *info = *next;
            }
        }
        history_stored_earliest = page->earliest;
        history_stored_latest = page->latest;
    }
    /* Take only queue bounds here; copy selected records under a later short
     * lock. The storage bus prevents a flush from moving RAM into CSV midway.
     */
    k_mutex_lock(&queue_lock, K_FOREVER);
    if (queue_count) {
        const struct card_sample *oldest = &queued[queue_head];
        const struct card_sample *latest = &queued[(queue_head + queue_count - 1) % SD_QUEUE_CAPACITY];
        if (!page->earliest.boot_id) { page->earliest = (struct sd_logger_cursor){current_boot_id, oldest->sequence}; }
        page->latest = (struct sd_logger_cursor){current_boot_id, latest->sequence};
    }
    k_mutex_unlock(&queue_lock);
    if (!error && after.boot_id && !resume) {
        int present = selected_file ? history_cursor_present(selected, after.sequence) : 0;
        if (present < 0) { error = present; }
        cursor_found = present > 0;
        if (!cursor_found && after.boot_id == current_boot_id) {
            k_mutex_lock(&queue_lock, K_FOREVER);
            for (size_t i = 0; i < queue_count; ++i) {
                if (queued[(queue_head + i) % SD_QUEUE_CAPACITY].sequence == after.sequence) {
                    cursor_found = true; break;
                }
            }
            k_mutex_unlock(&queue_lock);
        }
    }
    if (!cursor_found) {
        page->gap = true;
        after = (struct sd_logger_cursor){0};
        if (!first_error) {
            *selected = *first; selected_file = true;
            history_next_index = history_first_next_index;
        }
    }
    if (!error && selected_file) {
        uint32_t skip = selected->boot_id == after.boot_id ? after.sequence : 0;
        while (!error) {
            page->boot_id = selected->boot_id;
            if (skip < selected->last.sequence) {
                error = history_file_rows(selected, skip, samples, capacity, page);
            }
            if (error || page->count || selected->boot_id == current_boot_id) { break; }
            int result;
            if (history_next_index >= 0) { result = history_file_info(history_next_index, info); }
            else if (history_next_index == -1) { result = -ENODATA; }
            else { result = next_history_file(selected->index, info); }
            if (result == -ENODATA) { selected_file = false; break; }
            if (result) { error = result; break; }
            *selected = *info; skip = 0;
            history_next_index = -2; /* Discover once at this file's boundary. */
        }
    }
    if (!error && (!selected_file || page->boot_id == current_boot_id)) {
        page->boot_id = current_boot_id;
        uint32_t skip = page->count ? samples[page->count - 1].sequence :
            after.boot_id == current_boot_id ? after.sequence : 0;
        k_mutex_lock(&queue_lock, K_FOREVER);
        for (size_t i = 0; i < queue_count; ++i) {
            const struct card_sample *sample = &queued[(queue_head + i) % SD_QUEUE_CAPACITY];
            if (sample->sequence <= skip) { continue; }
            if (page->count == capacity) { page->more = true; break; }
            if (skip && sample->sequence != skip + 1U) { page->gap = true; }
            samples[page->count++] = *sample; skip = sample->sequence;
        }
        k_mutex_unlock(&queue_lock);
    }
    if (!error) {
        k_mutex_lock(&queue_lock, K_FOREVER);
        if (queue_count) {
            const struct card_sample *oldest = &queued[queue_head];
            const struct card_sample *latest = &queued[(queue_head + queue_count - 1) % SD_QUEUE_CAPACITY];
            if (!page->earliest.boot_id) { page->earliest = (struct sd_logger_cursor){current_boot_id, oldest->sequence}; }
            page->latest = (struct sd_logger_cursor){current_boot_id, latest->sequence};
        }
        k_mutex_unlock(&queue_lock);
        page->more = page->count && (page->boot_id != page->latest.boot_id ||
            samples[page->count - 1].sequence < page->latest.sequence);
    }
    int ended = unmount_card();
    if (!error && !ended) {
        history_scan_valid = true;
        history_selected_file = selected_file;
        history_resume = page->count ? (struct sd_logger_cursor){page->boot_id,
            samples[page->count - 1].sequence} : after;
    } else {
        history_cache_invalidate_locked();
    }
    sd_logger_bus_unlock();
    return error ? error : ended;
}

void sd_logger_history_reset(void)
{
    k_mutex_lock(&queue_lock, K_FOREVER);
    history_reset_requested = true;
    k_mutex_unlock(&queue_lock);
}
