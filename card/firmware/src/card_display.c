/* Card dashboard: latest measured value in each ten-minute slot, up to 24h. */
#include "card_display.h"
#include "sd_logger.h"
#include "card_image_transfer.h"

#include <epd3in6e.h>
#include <epd_dashboard.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

enum display_series {
	SERIES_TEMPERATURE,
	SERIES_HUMIDITY,
	SERIES_PRESSURE,
	SERIES_CO2,
	SERIES_VOC,
	SERIES_HCHO,
	SERIES_NOX,
	SERIES_LUX,
};

static struct epd3in6e display = {
	.spi = SPI_DT_SPEC_GET(DT_NODELABEL(epd_display), SPI_WORD_SET(8), 0),
	.dc = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_dc_gpios),
	.reset = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_reset_gpios),
	.busy = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_busy_gpios),
};
static struct epd_dashboard_series history[EPD_DASHBOARD_SERIES];
static struct epd_dashboard_series snapshot[EPD_DASHBOARD_SERIES];
static struct epd_dashboard_series saved_history[EPD_DASHBOARD_SERIES];
/* Exact timestamps identify slots and choose the newest row within each bin. */
static uint64_t sample_stamp[EPD_DASHBOARD_SAMPLES];
static uint64_t saved_stamp[EPD_DASHBOARD_SAMPLES];
/* Only this boot's bins follow a receiver clock correction. Saved records
 * from preceding boots retain their own timestamps and never inherit it.
 */
static uint8_t live_owned[EPD_DASHBOARD_VALIDITY_BYTES];
static uint8_t frame[EPD3IN6E_FRAME_BYTES];
static struct k_thread display_thread;
K_THREAD_STACK_DEFINE(display_stack, 4096);
K_MUTEX_DEFINE(history_mutex);
K_SEM_DEFINE(refresh_requested, 0, 1);
static bool initialized;
static bool gas_indices;
static bool have_sample;
static bool refresh_pending;
static bool requested_latest_anchor;
static enum card_display_mode requested_mode = CARD_DISPLAY_GRAPH;
static const uint8_t *image_frame;
static char image_path[48];
static uint64_t latest_stamp;
static uint64_t latest_saved_stamp;
static bool live_utc;
static bool saved_utc;
static bool have_saved;
static bool restoring;
static bool requested_saved;
static bool saved_merged;
static bool mode_selected_explicitly;
static uint32_t latest_sequence;
static uint32_t latest_uptime_s;
static bool enrollment_visible;
static bool passkey_requested;
static uint32_t passkey_value;
static uint32_t passkey_request_id;
static uint32_t requested_revision;

static void configure_series(struct epd_dashboard_series *series,
			     const char *name, const char *unit,
			     uint8_t color, uint8_t mix_color,
			     float plot_low, float plot_high,
			     float very_low, float low, float mid, float high,
			     uint8_t decimals)
{
	series->name = name;
	series->unit = unit;
	series->lineColor = color;
	series->mixColor = mix_color;
	series->plotLow = plot_low;
	series->plotHigh = plot_high;
	series->veryLowHigh = very_low;
	series->lowHigh = low;
	series->midHigh = mid;
	series->highHigh = high;
	series->decimals = decimals;
	series->has_validity_map = 1;
}

static void configure_metadata(bool use_gas_indices)
{
	configure_series(&history[SERIES_TEMPERATURE], "TEMP", "C",
			 EPD3IN6E_RED, EPD3IN6E_RED, -10.0f, 50.0f,
			 19.0f, 20.0f, 28.0f, 30.0f, 1);
	configure_series(&history[SERIES_HUMIDITY], "HUM", "%",
			 EPD3IN6E_BLUE, EPD3IN6E_BLUE, 0.0f, 100.0f,
			 37.5f, 40.0f, 65.0f, 75.0f, 0);
	configure_series(&history[SERIES_PRESSURE], "PRESS", "HPA",
			 EPD3IN6E_GREEN, EPD3IN6E_GREEN, 950.0f, 1050.0f,
			 1007.5f, 1009.0f, 1016.0f, 1017.5f, 0);
	configure_series(&history[SERIES_CO2], "CO2", "PPM",
			 EPD3IN6E_BLACK, EPD3IN6E_BLACK, 400.0f, 3000.0f,
			 500.0f, 600.0f, 1000.0f, 1500.0f, 0);
	if (use_gas_indices) {
		configure_series(&history[SERIES_VOC], "VOC", "IDX",
				 EPD3IN6E_YELLOW, EPD3IN6E_RED, 1.0f, 500.0f,
				 50.0f, 100.0f, 200.0f, 400.0f, 0);
		configure_series(&history[SERIES_NOX], "NOX", "IDX",
				 EPD3IN6E_BLUE, EPD3IN6E_GREEN, 1.0f, 500.0f,
				 1.0f, 10.0f, 100.0f, 300.0f, 0);
	} else {
		/* Raw signal bands are numeric levels, not gas concentrations. */
		configure_series(&history[SERIES_VOC], "VOC", "RAW",
				 EPD3IN6E_YELLOW, EPD3IN6E_RED, 0.0f, 65535.0f,
				 13107.0f, 26214.0f, 39321.0f, 52428.0f, 0);
		configure_series(&history[SERIES_NOX], "NOX", "RAW",
				 EPD3IN6E_BLUE, EPD3IN6E_GREEN, 0.0f, 65535.0f,
				 13107.0f, 26214.0f, 39321.0f, 52428.0f, 0);
	}
	configure_series(&history[SERIES_HCHO], "HCHO", "PPM",
			 EPD3IN6E_RED, EPD3IN6E_BLUE, 0.0f, 0.15f,
			 0.01f, 0.02f, 0.08f, 0.12f, 2);
	configure_series(&history[SERIES_LUX], "LUX", "LX",
			 EPD3IN6E_GREEN, EPD3IN6E_YELLOW, 0.0f, 70000.0f,
			 100.0f, 1000.0f, 30000.0f, 60000.0f, 0);
}

static uint16_t normalize(const struct epd_dashboard_series *series, float value)
{
	if (value <= series->plotLow) {
		return 0;
	}
	if (value >= series->plotHigh) {
		return UINT16_MAX;
	}
	return (uint16_t)(((value - series->plotLow) /
			   (series->plotHigh - series->plotLow)) * UINT16_MAX + 0.5f);
}

static void set_present(struct epd_dashboard_series *series, size_t index, bool present)
{
	uint8_t mask = (uint8_t)BIT(index % 8U);

	if (present) {
		series->valid_bits[index / 8U] |= mask;
	} else {
		series->valid_bits[index / 8U] &= (uint8_t)~mask;
	}
}

static void clear_live_locked(void)
{
    memset(live_owned, 0, sizeof(live_owned));
    for (size_t slot = 0; slot < EPD_DASHBOARD_SAMPLES; ++slot) {
        sample_stamp[slot] = UINT64_MAX;
    }
    for (size_t series = 0; series < EPD_DASHBOARD_SERIES; ++series) {
        memset(history[series].valid_bits, 0, sizeof(history[series].valid_bits));
        history[series].has_current_value = 0;
    }
    have_sample = false;
    saved_merged = false;
}

static void record_into(struct epd_dashboard_series *target, uint64_t *stamps,
                        const struct card_sample *sample, uint64_t stamp,
                        uint64_t latest)
{
	float values[EPD_DASHBOARD_SERIES] = {
		[SERIES_TEMPERATURE] = sample->temperature_mc / 1000.0f,
		[SERIES_HUMIDITY] = sample->humidity_mpercent / 1000.0f,
		[SERIES_PRESSURE] = sample->pressure_pa / 100.0f,
		[SERIES_CO2] = sample->co2_ppm,
		[SERIES_HCHO] = sample->hcho_millippb / 1000000.0f,
		[SERIES_LUX] = sample->millilux / 1000.0f,
	};
	bool present[EPD_DASHBOARD_SERIES] = {
		[SERIES_TEMPERATURE] = (sample->valid & CARD_VALID_TEMPERATURE) != 0,
		[SERIES_HUMIDITY] = (sample->valid & CARD_VALID_HUMIDITY) != 0,
		[SERIES_PRESSURE] = (sample->valid & CARD_VALID_PRESSURE) != 0,
		[SERIES_CO2] = (sample->valid & CARD_VALID_CO2) != 0 && sample->co2_ppm >= 0,
		[SERIES_HCHO] = (sample->valid & CARD_VALID_HCHO) != 0,
		[SERIES_LUX] = (sample->valid & CARD_VALID_LUX) != 0,
	};

	if (gas_indices) {
		values[SERIES_VOC] = sample->voc_index;
		values[SERIES_NOX] = sample->nox_index;
		present[SERIES_VOC] = (sample->valid & CARD_VALID_VOC_INDEX) != 0 &&
				     sample->voc_index >= 1 && sample->voc_index <= 500;
		present[SERIES_NOX] = (sample->valid & CARD_VALID_NOX_INDEX) != 0 &&
				     sample->nox_index >= 1 && sample->nox_index <= 500;
	} else {
		values[SERIES_VOC] = sample->raw_voc;
		values[SERIES_NOX] = sample->raw_nox;
		present[SERIES_VOC] = (sample->valid & CARD_VALID_VOC_RAW) != 0;
		present[SERIES_NOX] = (sample->valid & CARD_VALID_NOX_RAW) != 0;
	}
    size_t slot = (stamp / EPD_DASHBOARD_SLOT_SECONDS) % EPD_DASHBOARD_SAMPLES;
    /* Files are visited newest first; older rows must not replace a newer bin. */
    bool current_boot = target == history;
    bool owned = (live_owned[slot / 8] & BIT(slot % 8)) != 0;
    if (stamps[slot] != UINT64_MAX && stamps[slot] > stamp && (!current_boot || owned)) { return; }
    stamps[slot] = stamp;
    if (current_boot) { live_owned[slot / 8] |= (uint8_t)BIT(slot % 8); }
    for (size_t series = 0; series < EPD_DASHBOARD_SERIES; ++series) {
        target[series].values[slot] = present[series] ?
            normalize(&target[series], values[series]) : 0;
        set_present(&target[series], slot, present[series]);
        if (stamp >= latest) {
            target[series].has_current_value = present[series] ? 1 : 0;
            target[series].current_value = present[series] ? values[series] : 0.0f;
        }
    }
}

static void merge_saved_locked(void)
{
    if (restoring || saved_merged || !have_saved || !saved_utc || !live_utc || !have_sample) { return; }
    uint64_t oldest_bucket = latest_stamp / EPD_DASHBOARD_SLOT_SECONDS;
    oldest_bucket = oldest_bucket >= EPD_DASHBOARD_SAMPLES - 1 ?
        oldest_bucket - (EPD_DASHBOARD_SAMPLES - 1) : 0;
    for (size_t slot = 0; slot < EPD_DASHBOARD_SAMPLES; ++slot) {
        uint64_t stamp = saved_stamp[slot];
        if (stamp == UINT64_MAX || stamp > latest_stamp ||
            stamp / EPD_DASHBOARD_SLOT_SECONDS < oldest_bucket ||
            (sample_stamp[slot] != UINT64_MAX && sample_stamp[slot] >= stamp)) { continue; }
        sample_stamp[slot] = stamp;
        live_owned[slot / 8] &= (uint8_t)~BIT(slot % 8);
        for (size_t series = 0; series < EPD_DASHBOARD_SERIES; ++series) {
            history[series].values[slot] = saved_history[series].values[slot];
            set_present(&history[series], slot,
                (saved_history[series].valid_bits[slot / 8] & BIT(slot % 8)) != 0);
        }
    }
    saved_merged = true;
}

/* Only this boot's uptime has a known relation to the newly supplied UTC.
 * Preserve pre-sync measurements without inventing the preceding boot's gap.
 * Scratch is ~1.5 KiB on the measurement thread, used once at first sync. */
static void rebase_live_locked(int64_t offset)
{
    uint64_t previous_stamps[EPD_DASHBOARD_SAMPLES];
    uint16_t previous_values[EPD_DASHBOARD_SAMPLES];
    uint8_t previous_valid[EPD_DASHBOARD_VALIDITY_BYTES];
    uint8_t previous_owned[EPD_DASHBOARD_VALIDITY_BYTES];
    memcpy(previous_stamps, sample_stamp, sizeof(previous_stamps));
    memcpy(previous_owned, live_owned, sizeof(previous_owned));
    memset(live_owned, 0, sizeof(live_owned));
    for (size_t slot = 0; slot < EPD_DASHBOARD_SAMPLES; ++slot) { sample_stamp[slot] = UINT64_MAX; }
    for (size_t slot = 0; slot < EPD_DASHBOARD_SAMPLES; ++slot) {
        if (previous_stamps[slot] == UINT64_MAX) { continue; }
        bool owned = (previous_owned[slot / 8] & BIT(slot % 8)) != 0;
        int64_t moved = (int64_t)previous_stamps[slot] + (owned ? offset : 0);
        if (moved < 0) { continue; }
        uint64_t stamp = (uint64_t)moved;
        size_t destination = (stamp / EPD_DASHBOARD_SLOT_SECONDS) % EPD_DASHBOARD_SAMPLES;
        if (sample_stamp[destination] == UINT64_MAX || stamp > sample_stamp[destination]) {
            sample_stamp[destination] = stamp;
            if (owned) { live_owned[destination / 8] |= (uint8_t)BIT(destination % 8); }
            else { live_owned[destination / 8] &= (uint8_t)~BIT(destination % 8); }
        }
    }
    for (size_t series = 0; series < EPD_DASHBOARD_SERIES; ++series) {
        memcpy(previous_values, history[series].values, sizeof(previous_values));
        memcpy(previous_valid, history[series].valid_bits, sizeof(previous_valid));
        memset(history[series].valid_bits, 0, sizeof(history[series].valid_bits));
        for (size_t slot = 0; slot < EPD_DASHBOARD_SAMPLES; ++slot) {
            if (previous_stamps[slot] == UINT64_MAX) { continue; }
            bool owned = (previous_owned[slot / 8] & BIT(slot % 8)) != 0;
            int64_t moved = (int64_t)previous_stamps[slot] + (owned ? offset : 0);
            if (moved < 0) { continue; }
            uint64_t stamp = (uint64_t)moved;
            size_t destination = (stamp / EPD_DASHBOARD_SLOT_SECONDS) % EPD_DASHBOARD_SAMPLES;
            if (sample_stamp[destination] != stamp) { continue; }
            history[series].values[destination] = previous_values[slot];
            set_present(&history[series], destination,
                        (previous_valid[slot / 8] & BIT(slot % 8)) != 0);
        }
    }
    latest_stamp = (uint64_t)((int64_t)latest_stamp + offset);
    saved_merged = false;
}

void card_display_record(const struct card_sample *sample)
{
    if (!sample) { return; }
    k_mutex_lock(&history_mutex, K_FOREVER);
    if (!initialized) { k_mutex_unlock(&history_mutex); return; }
    bool utc = sample->unix_s != 0;
    bool first_utc = utc && !live_utc;
    uint64_t stamp = utc ? sample->unix_s : sample->uptime_s;
    /* Unknown boot uptimes are never concatenated with a preceding boot. */
    if (have_sample && utc != live_utc) {
        if (utc && sample->unix_s >= sample->uptime_s) {
            rebase_live_locked((int64_t)(sample->unix_s - sample->uptime_s));
        } else { clear_live_locked(); }
    }
    if (have_sample && utc && live_utc) {
        int64_t old_offset = (int64_t)latest_stamp - latest_uptime_s;
        int64_t new_offset = (int64_t)sample->unix_s - sample->uptime_s;
        if (new_offset != old_offset) { rebase_live_locked(new_offset - old_offset); }
    }
    if (have_sample && stamp < latest_stamp) { k_mutex_unlock(&history_mutex); return; }
    live_utc = utc;
    record_into(history, sample_stamp, sample, stamp, have_sample ? latest_stamp : 0);
    latest_stamp = stamp;
    latest_sequence = sample->sequence;
    latest_uptime_s = sample->uptime_s;
    have_sample = true;
    merge_saved_locked();
    k_mutex_unlock(&history_mutex);
    if (first_utc) { (void)sd_logger_restore_utc_async(); }
}

void card_display_saved_begin(bool utc_axis, uint64_t anchor_s)
{
    k_mutex_lock(&history_mutex, K_FOREVER);
    restoring = initialized;
    have_saved = false;
    saved_merged = false;
    saved_utc = utc_axis;
    latest_saved_stamp = anchor_s;
    for (size_t slot = 0; slot < EPD_DASHBOARD_SAMPLES; ++slot) { saved_stamp[slot] = UINT64_MAX; }
    for (size_t series = 0; series < EPD_DASHBOARD_SERIES; ++series) {
        saved_history[series] = history[series];
        memset(saved_history[series].valid_bits, 0, sizeof(saved_history[series].valid_bits));
        saved_history[series].has_current_value = 0;
    }
    k_mutex_unlock(&history_mutex);
}

void card_display_saved_record(const struct card_sample *sample)
{
    if (!sample) { return; }
    k_mutex_lock(&history_mutex, K_FOREVER);
    uint64_t stamp = saved_utc ? sample->unix_s : sample->uptime_s;
    uint64_t anchor_bucket = latest_saved_stamp / EPD_DASHBOARD_SLOT_SECONDS;
    uint64_t bucket = stamp / EPD_DASHBOARD_SLOT_SECONDS;
    if (restoring && (!saved_utc || sample->unix_s != 0) && stamp <= latest_saved_stamp &&
        bucket <= anchor_bucket && anchor_bucket - bucket < EPD_DASHBOARD_SAMPLES) {
        record_into(saved_history, saved_stamp, sample, stamp, latest_saved_stamp);
        have_saved = true;
    }
    k_mutex_unlock(&history_mutex);
}

void card_display_saved_end(void)
{
    k_mutex_lock(&history_mutex, K_FOREVER);
    restoring = false;
    merge_saved_locked();
    bool request = initialized && have_saved && !mode_selected_explicitly &&
        requested_mode == CARD_DISPLAY_GRAPH && !enrollment_visible;
    bool wake = request && !refresh_pending;
    if (request) {
        requested_mode = CARD_DISPLAY_GRAPH;
        requested_saved = true;
        requested_latest_anchor = true;
        ++requested_revision;
        refresh_pending = true;
    }
    k_mutex_unlock(&history_mutex);
    if (wake) { k_sem_give(&refresh_requested); }
}

/* Worker-only snapshot; the mutex is held briefly and all I/O is outside it. */
static uint32_t take_snapshot(uint64_t now_s, bool saved)
{
    struct epd_dashboard_series *source = saved ? saved_history : history;
    const uint64_t *stamps = saved ? saved_stamp : sample_stamp;
    uint64_t latest = saved ? latest_saved_stamp : latest_stamp;
    uint64_t now_bucket = now_s / EPD_DASHBOARD_SLOT_SECONDS;
    for (size_t series = 0; series < EPD_DASHBOARD_SERIES; ++series) {
        snapshot[series] = source[series];
        memset(snapshot[series].valid_bits, 0, sizeof(snapshot[series].valid_bits));
        snapshot[series].has_current_value = latest / EPD_DASHBOARD_SLOT_SECONDS == now_bucket ?
            source[series].has_current_value : 0;
    }
    uint32_t recorded_slots = 0;
    for (size_t column = 0; column < EPD_DASHBOARD_SAMPLES; ++column) {
        uint64_t age = EPD_DASHBOARD_SAMPLES - 1U - column;
        if (age > now_bucket) { continue; }
        uint64_t bucket = now_bucket - age;
        size_t slot = bucket % EPD_DASHBOARD_SAMPLES;
        if (stamps[slot] == UINT64_MAX || stamps[slot] / EPD_DASHBOARD_SLOT_SECONDS != bucket) { continue; }
        ++recorded_slots;
        for (size_t series = 0; series < EPD_DASHBOARD_SERIES; ++series) {
            snapshot[series].values[column] = source[series].values[slot];
            set_present(&snapshot[series], column,
                (source[series].valid_bits[slot / 8U] & BIT(slot % 8U)) != 0);
        }
    }
    return recorded_slots;
}

/* Same clockwise rotation as the dashboard's 600x400 logical canvas. */
static void image_pixel(uint16_t x, uint16_t y, uint8_t color)
{
	uint16_t native_x = EPD3IN6E_WIDTH - 1U - y;
	size_t index = x * EPD3IN6E_ROW_BYTES + native_x / 2U;

	if ((native_x & 1U) == 0) {
		frame[index] = (uint8_t)((frame[index] & 0x0fU) | (color << 4));
	} else {
		frame[index] = (uint8_t)((frame[index] & 0xf0U) | color);
	}
}

static void image_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
		       uint8_t color)
{
	for (uint16_t row = y; row < y + height; ++row) {
		for (uint16_t col = x; col < x + width; ++col) {
			image_pixel(col, row, color);
		}
	}
}

static void image_unset_label(void)
{
	/* Small local bitmap alphabet used only for the missing-image label. */
	static const struct {
		char letter;
		uint8_t rows[7];
	} font[] = {
		{'I', {31, 4, 4, 4, 4, 4, 31}},
		{'M', {17, 27, 21, 17, 17, 17, 17}},
		{'A', {14, 17, 17, 31, 17, 17, 17}},
		{'G', {14, 17, 16, 23, 17, 17, 15}},
		{'E', {31, 16, 16, 30, 16, 16, 31}},
		{'N', {17, 25, 25, 21, 19, 19, 17}},
		{'O', {14, 17, 17, 17, 17, 17, 14}},
		{'T', {31, 4, 4, 4, 4, 4, 4}},
		{'S', {15, 16, 16, 14, 1, 1, 30}},
	};
	static const char text[] = "IMAGE NOT SET";
	const uint16_t scale = 4;
	uint16_t x = (EPD3IN6E_HEIGHT - (sizeof(text) - 1U) * 6U * scale) / 2U;

	for (size_t letter = 0; letter < sizeof(text) - 1U; ++letter) {
		for (size_t glyph = 0; glyph < ARRAY_SIZE(font); ++glyph) {
			if (font[glyph].letter != text[letter]) {
				continue;
			}
			for (size_t row = 0; row < 7; ++row) {
				for (size_t col = 0; col < 5; ++col) {
					if ((font[glyph].rows[row] & BIT(4U - col)) != 0) {
						image_rect(x + col * scale, 320U + row * scale,
							   scale, scale, EPD3IN6E_BLACK);
					}
				}
			}
			break;
		}
		x += 6U * scale;
	}
}

static void render_unset_image(void)
{
	memset(frame, (EPD3IN6E_WHITE << 4) | EPD3IN6E_WHITE, sizeof(frame));
	image_rect(12, 12, 576, 376, EPD3IN6E_BLACK);
	image_rect(16, 16, 568, 368, EPD3IN6E_WHITE);
	image_rect(30, 30, 540, 270, EPD3IN6E_BLUE);
	for (uint16_t y = 50; y < 300; ++y) {
		for (uint16_t x = 30; x < 570; ++x) {
			int dx = (int)x - 450;
			int dy = (int)y - 110;

			if (dx * dx + dy * dy <= 45 * 45) {
				image_pixel(x, y, EPD3IN6E_YELLOW);
			}
			int distance = (int)x - 180;

			if (distance < 0) {
				distance = -distance;
			}
			if ((int)y >= 105 + distance * 2 / 3) {
				image_pixel(x, y, EPD3IN6E_GREEN);
			}
			distance = (int)x - 410;
			if (distance < 0) {
				distance = -distance;
			}
			if ((int)y >= 165 + distance * 3 / 4) {
				image_pixel(x, y, EPD3IN6E_RED);
			}
		}
	}
	image_unset_label();
}

static uint32_t refresh_once(void)
{
	k_mutex_lock(&history_mutex, K_FOREVER);
	enum card_display_mode mode = requested_mode;
	const uint8_t *selected_image = image_frame;
    char selected_path[sizeof(image_path)];
    memcpy(selected_path, image_path, sizeof(selected_path));
	bool saved = requested_saved && have_saved;
    uint64_t now_s = saved ? latest_saved_stamp : latest_stamp;
    if (!saved && !requested_latest_anchor && have_sample) {
        uint64_t uptime = (uint64_t)(k_uptime_get() / 1000);
        if (uptime >= latest_uptime_s) { now_s += uptime - latest_uptime_s; }
    }
    uint32_t recorded_slots = mode == CARD_DISPLAY_GRAPH ? take_snapshot(now_s, saved) : 0;
    bool utc_axis = saved ? saved_utc : live_utc;
	uint32_t sequence = latest_sequence;
	uint32_t uptime_s = latest_uptime_s;
	bool indices = gas_indices;
	bool pairing = enrollment_visible && passkey_requested;
	uint32_t passkey = passkey_value;
	uint32_t request_id = passkey_request_id;
	uint32_t revision = requested_revision;

	k_mutex_unlock(&history_mutex);
	int err = 0;
	const uint8_t *rendered_frame = frame;
    uint32_t image_request_token = 0;

	if (pairing) {
		printk("CARD DISPLAY: pairing PIN request=%u (PIN omitted from logs)\n", request_id);
		err = epd_dashboard_pairing_render(frame, sizeof(frame), passkey);
	} else if (mode == CARD_DISPLAY_GRAPH) {
		printk("CARD DISPLAY: %s graph, 24h of %s, %u ten-minute slots; seq=%u uptime=%u s\n",
               saved ? "saved" : "current", utc_axis ? "UTC" : "one boot uptime",
               recorded_slots, sequence, uptime_s);
		printk("CARD DISPLAY: measured data; VOC/NOX %s; HCHO absent when not installed (--/NA)\n",
		       indices ? "IDX" : "RAW");
		const char *caption = saved ? (utc_axis ? "SAVED UTC HISTORY / 10 MIN POINTS" :
            "SAVED SESSION / CLOCK UNKNOWN; POWER-OFF GAP UNKNOWN") :
            (utc_axis ? "CURRENT UTC HISTORY / 10 MIN POINTS" : "CURRENT SESSION / CLOCK UNKNOWN");
        err = epd_dashboard_render_labeled(frame, sizeof(frame), snapshot, ARRAY_SIZE(snapshot),
                                            caption, saved ? "LAST" : "NOW");
	} else if (selected_path[0]) {
		image_request_token = card_image_display_started();
        printk("CARD DISPLAY: loading saved image into existing frame\n");
        err = card_image_load_file(selected_path, frame, sizeof(frame));
    } else if (selected_image != NULL) {
		printk("CARD DISPLAY: image, caller-supplied native frame\n");
		rendered_frame = selected_image;
	} else {
		printk("CARD DISPLAY: image not set; built-in placeholder\n");
		render_unset_image();
	}

    /* SD and EPD share SPI20. Keep the whole panel transaction serialized. */
    sd_logger_bus_lock();
	if (err == 0) {
		err = epd3in6e_init(&display);
	}
	if (err == 0) {
		err = epd3in6e_refresh_frame(&display, rendered_frame, sizeof(frame));
	}
	if (display.initialized) {
		int sleep_err = epd3in6e_sleep(&display);

		if (err == 0) {
			err = sleep_err;
		}
	}
    sd_logger_bus_unlock();
	if (err == 0) {
		printk("CARD DISPLAY: %s refresh completed; panel deep sleep\n",
		       pairing ? "pairing" : mode == CARD_DISPLAY_GRAPH ? "graph" : "image");
	} else {
		printk("CARD DISPLAY: refresh failed: %d\n", err);
	}
	if (pairing) { env_telemetry_ble_pairing_display_ready(request_id, err); }
    else if (mode == CARD_DISPLAY_IMAGE && selected_path[0]) {
        card_image_display_result(image_request_token, err);
    }
	return revision;
}

static void process_refresh_requests(void)
{
	/* A mode/image change while BUSY is handled after this complete refresh. */
	bool follow_up;

	do {
		uint32_t revision = refresh_once();

		k_mutex_lock(&history_mutex, K_FOREVER);
		follow_up = requested_revision != revision &&
			(!enrollment_visible || passkey_requested);
		refresh_pending = follow_up;
		k_mutex_unlock(&history_mutex);
	} while (follow_up);
}

static void display_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	while (true) {
		k_sem_take(&refresh_requested, K_FOREVER);
		process_refresh_requests();
	}
}

int card_display_init(bool use_gas_indices)
{
	k_mutex_lock(&history_mutex, K_FOREVER);
	if (initialized) {
		k_mutex_unlock(&history_mutex);
		return -EALREADY;
	}
    clear_live_locked();
	configure_metadata(use_gas_indices);
	gas_indices = use_gas_indices;
	initialized = true;
	k_thread_create(&display_thread, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
			display_worker, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	k_mutex_unlock(&history_mutex);
	return 0;
}

static int request_refresh(bool periodic)
{
	k_mutex_lock(&history_mutex, K_FOREVER);
	int err = !initialized ? -EACCES : enrollment_visible ? -EBUSY : 0;
	bool wake = false;

	/* Sensor recording and periodic BLE/SD activity cannot change an image.
	 * In particular, leave an in-flight explicit image request and its revision
	 * untouched, so the worker does not repeat it after the panel goes idle.
	 */
	if (err == 0 && (!periodic || requested_mode == CARD_DISPLAY_GRAPH)) {
		err = !have_sample && !have_saved ? -ENODATA :
			refresh_pending ? -EALREADY : 0;
		if (err == 0) {
			refresh_pending = true;
			requested_latest_anchor = false;
			requested_saved = !have_sample && have_saved;
			wake = true;
		}
	}
	k_mutex_unlock(&history_mutex);
	if (wake) {
		k_sem_give(&refresh_requested);
	}
	return err;
}

int card_display_refresh(void)
{
	return request_refresh(false);
}

int card_display_refresh_periodic(void)
{
	return request_refresh(true);
}

static bool request_mode_locked(enum card_display_mode mode)
{
	requested_mode = mode;
	requested_latest_anchor = true;
	if (enrollment_visible) { return false; }
	++requested_revision;
	if (refresh_pending) {
		return false;
	}
	refresh_pending = true;
	return true;
}

void card_display_pairing_event(enum env_telemetry_pairing_event event,
                                unsigned int passkey, uint32_t request_id,
                                void *context)
{
	ARG_UNUSED(context);
	k_mutex_lock(&history_mutex, K_FOREVER);
	if (!initialized) {
		k_mutex_unlock(&history_mutex);
		if (event == ENV_TELEMETRY_PAIRING_PASSKEY) {
			env_telemetry_ble_pairing_display_ready(request_id, -ENODEV);
		}
		return;
	}
	bool wake = false;
	if (event == ENV_TELEMETRY_PAIRING_OPENED) {
		enrollment_visible = true;
		passkey_requested = false;
	} else if (event == ENV_TELEMETRY_PAIRING_PASSKEY) {
		enrollment_visible = true;
		passkey_requested = true;
		passkey_value = passkey;
		passkey_request_id = request_id;
		++requested_revision;
		wake = !refresh_pending;
		refresh_pending = true;
	} else {
		enrollment_visible = false;
		passkey_requested = false;
		++requested_revision;
		wake = !refresh_pending;
		refresh_pending = true;
	}
	k_mutex_unlock(&history_mutex);
	if (wake) { k_sem_give(&refresh_requested); }
}

int card_display_set_mode(enum card_display_mode mode)
{
	if (mode != CARD_DISPLAY_GRAPH && mode != CARD_DISPLAY_IMAGE) {
		return -EINVAL;
	}
	k_mutex_lock(&history_mutex, K_FOREVER);
	int err = initialized ? 0 : -EACCES;
    if (!err) { mode_selected_explicitly = true; }
	bool wake_worker = err == 0 && request_mode_locked(mode);

	k_mutex_unlock(&history_mutex);
	if (wake_worker) {
		k_sem_give(&refresh_requested);
	}
	return err;
}

static bool image_color_valid(uint8_t color)
{
	return color == EPD3IN6E_BLACK || color == EPD3IN6E_WHITE ||
	       color == EPD3IN6E_YELLOW || color == EPD3IN6E_RED ||
	       color == EPD3IN6E_BLUE || color == EPD3IN6E_GREEN;
}

int card_display_set_image(const uint8_t *image, size_t length)
{
	if (image == NULL ? length != 0 : length != EPD3IN6E_FRAME_BYTES) {
		return -EINVAL;
	}
	for (size_t i = 0; i < length; ++i) {
		if (!image_color_valid(image[i] >> 4) || !image_color_valid(image[i] & 0x0fU)) {
			return -EINVAL;
		}
	}
	k_mutex_lock(&history_mutex, K_FOREVER);
	int err = !initialized ? -EACCES : refresh_pending ? -EBUSY : 0;
	bool wake_worker = false;

	if (err == 0) {
		image_frame = image;
        image_path[0] = '\0';
		if (requested_mode == CARD_DISPLAY_IMAGE) {
			wake_worker = request_mode_locked(CARD_DISPLAY_IMAGE);
		}
	}
	k_mutex_unlock(&history_mutex);
	if (wake_worker) {
		k_sem_give(&refresh_requested);
	}
	return err;
}

int card_display_set_image_file(const char *path)
{
    if (!path || strncmp(path, "/SD:/", 5) ||
        strnlen(path, sizeof(image_path)) >= sizeof(image_path)) { return -EINVAL; }
    k_mutex_lock(&history_mutex, K_FOREVER);
    int error = initialized ? 0 : -EACCES;
    bool wake = false;
    if (!error) {
        mode_selected_explicitly = true;
        strcpy(image_path, path);
        image_frame = NULL;
        wake = request_mode_locked(CARD_DISPLAY_IMAGE);
    }
    k_mutex_unlock(&history_mutex);
    if (wake) { k_sem_give(&refresh_requested); }
    return error;
}

int card_display_register_image_file(const char *path)
{
    if (!path || strncmp(path, "/SD:/", 5) ||
        strnlen(path, sizeof(image_path)) >= sizeof(image_path)) { return -EINVAL; }
    k_mutex_lock(&history_mutex, K_FOREVER);
    int error = initialized ? 0 : -EACCES;
    if (!error) { strcpy(image_path, path); }
    k_mutex_unlock(&history_mutex);
    return error;
}

void card_display_button(enum card_switch_contact contact, bool pressed,
			 uint32_t count, void *context)
{
	ARG_UNUSED(count);
	ARG_UNUSED(context);
	if (!pressed) {
		return;
	}
	if (contact < CARD_SWITCH_UP || contact > CARD_SWITCH_PUSH) {
		return;
	}
	k_mutex_lock(&history_mutex, K_FOREVER);
	if (!initialized) {
		k_mutex_unlock(&history_mutex);
		return;
	}
    mode_selected_explicitly = true;
    if (contact == CARD_SWITCH_UP) { requested_saved = have_saved; }
    else if (contact == CARD_SWITCH_DOWN) { requested_saved = false; }
	enum card_display_mode mode = contact == CARD_SWITCH_UP ? CARD_DISPLAY_GRAPH :
		contact == CARD_SWITCH_DOWN ? CARD_DISPLAY_GRAPH :
		requested_mode == CARD_DISPLAY_GRAPH ? CARD_DISPLAY_IMAGE : CARD_DISPLAY_GRAPH;
	bool wake_worker = request_mode_locked(mode);

	k_mutex_unlock(&history_mutex);
	printk("CARD DISPLAY: button selects %s%s\n",
	       mode == CARD_DISPLAY_GRAPH ? "graph" : "image",
	       wake_worker ? "" : "; coalesced while refreshing");
	if (wake_worker) {
		k_sem_give(&refresh_requested);
	}
}
