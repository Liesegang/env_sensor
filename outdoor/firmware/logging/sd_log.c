#include <sd_log.h>
#include <sd_csv.h>
#include <outdoor_clock.h>
#include <errno.h>
#include <ff.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define MINUTE_MS 60000
#define ROWS_PER_BATCH 64
struct row { int64_t unix_ms; uint16_t size; uint8_t packet[SD_LOG_PACKET_MAX]; };
struct batch { size_t count; struct row rows[ROWS_PER_BATCH]; };
K_MEM_SLAB_DEFINE(batches, sizeof(struct batch), 3, 8);
K_MSGQ_DEFINE(pending, sizeof(struct batch *), 2, sizeof(void *));
static struct batch *active;
static uint64_t deadline;
static uint32_t dropped;
static FATFS fatfs;
static struct fs_mount_t mount = {.type = FS_FATFS, .fs_data = &fatfs, .mnt_point = "/SD:"};
static struct fs_file_t file;
static bool mounted, opened;
static char filename[40];
/* A short filename and fixed CSV schema keep cards readable on any FAT host. */
static char line[4096];
static uint8_t write_buffer[4096];
static size_t buffered;

DWORD get_fattime(void)
{
	int64_t epoch;
	if (!outdoor_clock_at(k_uptime_get(), &epoch)) { return (40U << 25) | (1U << 21) | (1U << 16); }
	time_t seconds = epoch / 1000;
	struct tm utc;
	if (!gmtime_r(&seconds, &utc)) { return 0; }
	return ((uint32_t)(utc.tm_year - 80) << 25) | ((uint32_t)(utc.tm_mon + 1) << 21) |
		((uint32_t)utc.tm_mday << 16) | ((uint32_t)utc.tm_hour << 11) | ((uint32_t)utc.tm_min << 5) | (utc.tm_sec / 2);
}

void sd_log_submit(const struct telemetry_sample *sample)
{
	if (!sample) { return; }
	if (active && sample->uptime_ms >= deadline) {
		if (k_msgq_put(&pending, &active, K_NO_WAIT)) {
			dropped += active->count;
			k_mem_slab_free(&batches, active);
			printk("SD LOG: queue full, total dropped=%u\n", dropped);
		}
		active = NULL;
	}
	if (!active) {
		if (k_mem_slab_alloc(&batches, (void **)&active, K_NO_WAIT)) {
			dropped++;
			printk("SD LOG: buffers full, total dropped=%u\n", dropped);
			return;
		}
		active->count = 0;
		deadline = sample->uptime_ms + MINUTE_MS;
	}
	if (active->count >= ROWS_PER_BATCH) { dropped++; return; }
	struct row *row = &active->rows[active->count];
	row->size = telemetry_encode(sample, row->packet, sizeof(row->packet));
	if (!row->size) { dropped++; return; }
	row->unix_ms = -1;
	outdoor_clock_at(sample->uptime_ms, &row->unix_ms);
	active->count++;
}

static int write_all(const void *data, size_t length)
{
	const uint8_t *p = data;
	while (length) {
		ssize_t n = fs_write(&file, p, length);
		if (n < 0) { return n; }
		if (!n) { return -ENOSPC; }
		p += n;
		length -= n;
	}
	return 0;
}
static int flush_buffer(void)
{
	int err = write_all(write_buffer, buffered);
	buffered = 0;
	return err;
}
static int append_buffer(const char *data, size_t size)
{
	while (size) {
		size_t n = MIN(size, sizeof(write_buffer) - buffered);
		memcpy(write_buffer + buffered, data, n);
		buffered += n; data += n; size -= n;
		if (buffered == sizeof(write_buffer)) {
			int err = flush_buffer();
			if (err) { return err; }
		}
	}
	return 0;
}
static int open_log(void)
{
	int err;
	if (!mounted) {
		err = fs_mount(&mount);
		if (err) { return err; }
		mounted = true;
		printk("SD LOG: FAT card mounted\n");
	}
	if (opened) { return 0; }
	err = fs_mkdir("/SD:/OUTDOOR");
	if (err && err != -EEXIST) { return err; }
	struct fs_dirent entry;
	unsigned int index;
	for (index = 1; index <= 99999999; index++) {
		snprintf(filename, sizeof(filename), "/SD:/OUTDOOR/%08u.CSV", index);
		err = fs_stat(filename, &entry);
		if (err == -ENOENT) { break; }
		if (err) { return err; }
	}
	if (index > 99999999) { return -ENOSPC; }
	fs_file_t_init(&file);
	err = fs_open(&file, filename, FS_O_CREATE | FS_O_WRITE);
	if (err) { return err; }
	opened = true;
	err = write_all(sd_csv_header(), strlen(sd_csv_header()));
	if (!err) { err = fs_sync(&file); }
	if (!err) { printk("SD LOG: new CSV %s\n", filename); }
	return err;
}
static void storage_failed(void)
{
	buffered = 0;
	if (opened) { fs_close(&file); opened = false; }
	if (mounted) {
		int err = fs_unmount(&mount);
		if (!err) { mounted = false; }
		else { printk("SD LOG: unmount failed: %d\n", err); }
	}
}
static void logger(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (;;) {
		struct batch *batch;
		k_msgq_get(&pending, &batch, K_FOREVER);
		for (;;) {
			int err = open_log();
			off_t checkpoint = err ? -1 : fs_tell(&file);
			if (!err && checkpoint < 0) { err = checkpoint; }
			for (size_t i = 0; !err && i < batch->count; i++) {
				struct row *row = &batch->rows[i];
				int size = sd_csv_row(row->packet, row->size, row->unix_ms, line, sizeof(line));
				err = size < 0 ? size : append_buffer(line, size);
			}
			if (!err) { err = flush_buffer(); }
			if (!err) { err = fs_sync(&file); }
			if (!err) {
				printk("SD LOG: wrote %u samples to %s (synced)\n", (unsigned int)batch->count, filename);
				break;
			}
			/* Best-effort rollback; after a media error, retry this batch in a new file. */
			if (opened && checkpoint >= 0) {
				int rollback = fs_truncate(&file, checkpoint);
				if (!rollback) { fs_sync(&file); }
				else { printk("SD LOG: partial CSV tail possible: %d\n", rollback); }
			}
			printk("SD LOG: write/mount failed: %d; retry in 10 s\n", err);
			storage_failed();
			k_sleep(K_SECONDS(10));
		}
		k_mem_slab_free(&batches, batch);
	}
}
K_THREAD_DEFINE(sd_logger, 6144, logger, NULL, NULL, NULL, 8, 0, 0);
