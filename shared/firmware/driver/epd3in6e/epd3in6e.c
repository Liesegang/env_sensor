/* SPDX-License-Identifier: MIT
 * Reset, register values and refresh order adapted from Waveshare's
 * EPD_3in6e driver via the supplied Card sample. See LICENSE and README.md.
 */

#include "epd3in6e.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define BUSY_TIMEOUT_MS 60000
#define BUSY_ASSERT_TIMEOUT_MS 2000
#define ABORT_TIMEOUT_MS 3000

struct panel_command {
	uint8_t command;
	uint8_t length;
	uint8_t data[6];
};

static const struct panel_command init_commands[] = {
	{0xAA, 6, {0x49, 0x55, 0x20, 0x08, 0x09, 0x18}},
	{0x01, 1, {0x3F}},
	{0x00, 2, {0x5F, 0x69}},
	{0x05, 4, {0x40, 0x1F, 0x1F, 0x2C}},
	{0x08, 4, {0x6F, 0x1F, 0x1F, 0x22}},
	{0x06, 4, {0x6F, 0x1F, 0x17, 0x17}},
	{0x03, 4, {0x00, 0x54, 0x00, 0x44}},
	{0x60, 2, {0x02, 0x00}},
	{0x30, 1, {0x08}},
	{0x50, 1, {0x3F}},
	{0x61, 4, {0x01, 0x90, 0x02, 0x58}},
	{0xE3, 1, {0x2F}},
	{0x84, 1, {0x01}},
};

static int send_bytes(struct epd3in6e *display, bool data,
		      const uint8_t *bytes, size_t length)
{
	/* DC has physical HIGH=data / LOW=command, independent of DT polarity. */
	int err = gpio_pin_set_raw(display->dc.port, display->dc.pin, data ? 1 : 0);

	if (err != 0) {
		return err;
	}
	struct spi_buf buffer = {.buf = (void *)bytes, .len = length};
	const struct spi_buf_set buffers = {.buffers = &buffer, .count = 1};

	return spi_write_dt(&display->spi, &buffers);
}

static int send_command(struct epd3in6e *display, uint8_t command,
			const uint8_t *data, size_t length)
{
	int err = send_bytes(display, false, &command, 1);

	if (err == 0 && length != 0) {
		err = send_bytes(display, true, data, length);
	}
	return err;
}

static int wait_idle(struct epd3in6e *display, const char *phase,
		     int32_t timeout_ms, bool require_busy)
{
	const int64_t started = k_uptime_get();
	bool saw_busy = false;

	while (true) {
		/* ACTIVE_LOW: logical 1 is physical LOW (busy). */
		int busy = gpio_pin_get_dt(&display->busy);

		if (busy < 0) {
			return busy;
		}
		if (busy != 0 && !saw_busy) {
			saw_busy = true;
			printk("EPD3IN6E: %s BUSY asserted\n", phase);
		}
		const int64_t elapsed = k_uptime_get() - started;

		if (busy == 0 && (!require_busy || saw_busy)) {
			k_msleep(100);
			printk("EPD3IN6E: %s idle after %lld ms\n", phase, elapsed);
			return 0;
		}
		if (require_busy && !saw_busy && elapsed >= BUSY_ASSERT_TIMEOUT_MS) {
			printk("EPD3IN6E: %s did not assert BUSY\n", phase);
			return -ENODEV;
		}
		if (elapsed >= timeout_ms) {
			printk("EPD3IN6E: %s BUSY timeout\n", phase);
			return -ETIMEDOUT;
		}
		k_msleep(10);
	}
}

static int power_off(struct epd3in6e *display, int32_t timeout_ms)
{
	const uint8_t data = 0x00;
	int err = send_command(display, 0x02, &data, 1);

	if (err == 0) {
		err = wait_idle(display, "power off", timeout_ms, false);
	}
	return err;
}

static int deep_sleep(struct epd3in6e *display)
{
	const uint8_t check_code = 0xA5;

	return send_command(display, 0x07, &check_code, 1);
}

int epd3in6e_init(struct epd3in6e *display)
{
	if (display == NULL) {
		return -EINVAL;
	}
	display->initialized = false;
	if ((display->reset.dt_flags & GPIO_ACTIVE_LOW) == 0 ||
	    (display->busy.dt_flags & GPIO_ACTIVE_LOW) == 0) {
		return -EINVAL;
	}
	/* Reject bus configurations incompatible with the sample's mode 0. */
	if (SPI_WORD_SIZE_GET(display->spi.config.operation) != 8 ||
	    (display->spi.config.operation &
	     (SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_LSB |
	      SPI_OP_MODE_SLAVE | SPI_LOCK_ON | SPI_HOLD_ON_CS)) != 0 ||
	    display->spi.config.frequency == 0 || display->spi.config.frequency > 4000000) {
		return -EINVAL;
	}
	if (!spi_is_ready_dt(&display->spi) || !gpio_is_ready_dt(&display->dc) ||
	    !gpio_is_ready_dt(&display->reset) || !gpio_is_ready_dt(&display->busy)) {
		return -ENODEV;
	}
	int err = gpio_pin_configure_dt(&display->dc, GPIO_OUTPUT_LOW);

	if (err == 0) {
		err = gpio_pin_configure_dt(&display->reset, GPIO_OUTPUT_INACTIVE);
	}
	if (err == 0) {
		err = gpio_pin_configure_dt(&display->busy, GPIO_INPUT);
	}
	if (err != 0) {
		return err;
	}

	/* Physical HIGH 200 ms, LOW 20 ms, HIGH 200 ms, as in the sample. */
	k_msleep(200);
	err = gpio_pin_set_dt(&display->reset, 1);
	if (err != 0) {
		return err;
	}
	k_msleep(20);
	err = gpio_pin_set_dt(&display->reset, 0);
	if (err != 0) {
		return err;
	}
	k_msleep(200);
	err = wait_idle(display, "reset", BUSY_TIMEOUT_MS, false);
	if (err != 0) {
		return err;
	}
	k_msleep(30);
	for (size_t i = 0; i < ARRAY_SIZE(init_commands); ++i) {
		const struct panel_command *entry = &init_commands[i];

		err = send_command(display, entry->command, entry->data, entry->length);
		if (err != 0) {
			printk("EPD3IN6E: command 0x%02x failed: %d\n", entry->command, err);
			return err;
		}
	}
	err = wait_idle(display, "initialization", BUSY_TIMEOUT_MS, false);
	if (err == 0) {
		display->initialized = true;
	}
	return err;
}

static bool valid_color(uint8_t color)
{
	return color <= EPD3IN6E_RED || color == EPD3IN6E_BLUE || color == EPD3IN6E_GREEN;
}

int epd3in6e_refresh(struct epd3in6e *display, epd3in6e_row_fn row_fn, void *context)
{
	if (display == NULL || row_fn == NULL) {
		return -EINVAL;
	}
	if (!display->initialized) {
		return -EACCES;
	}
	uint8_t row[EPD3IN6E_ROW_BYTES];
	bool powered_on = false;
	int err = send_command(display, 0x10, NULL, 0);

	if (err != 0) {
		goto failed;
	}
	for (uint16_t y = 0; y < EPD3IN6E_HEIGHT; ++y) {
		err = row_fn(context, y, row);
		if (err != 0) {
			/* Positive callback values are not valid errno results. */
			err = err < 0 ? err : -EINVAL;
			goto failed;
		}
		for (size_t i = 0; i < sizeof(row); ++i) {
			if (!valid_color(row[i] >> 4) || !valid_color(row[i] & 0x0F)) {
				err = -EINVAL;
				goto failed;
			}
		}
		err = send_bytes(display, true, row, sizeof(row));
		if (err != 0) {
			printk("EPD3IN6E: frame row %u failed: %d\n", y, err);
			goto failed;
		}
	}

	/* Mark before the write: a failed SPI operation may still reach the panel. */
	powered_on = true;
	err = send_command(display, 0x04, NULL, 0);
	if (err == 0) {
		err = wait_idle(display, "power on", BUSY_TIMEOUT_MS, true);
	}
	if (err != 0) {
		goto failed;
	}
	k_msleep(200);
	const uint8_t second_booster_setting[] = {0x6F, 0x1F, 0x16, 0x29};

	err = send_command(display, 0x06, second_booster_setting,
			   sizeof(second_booster_setting));
	if (err != 0) {
		goto failed;
	}
	k_msleep(200);
	const uint8_t refresh_data = 0x00;

	err = send_command(display, 0x12, &refresh_data, 1);
	if (err == 0) {
		err = wait_idle(display, "refresh", BUSY_TIMEOUT_MS, true);
	}
	if (err != 0) {
		goto failed;
	}
	err = power_off(display, BUSY_TIMEOUT_MS);
	if (err == 0) {
		return 0;
	}

failed:
	display->initialized = false;
	if (powered_on) {
		int cleanup_err = power_off(display, ABORT_TIMEOUT_MS);

		if (cleanup_err == 0) {
			cleanup_err = deep_sleep(display);
		}
		if (cleanup_err != 0) {
			printk("EPD3IN6E: abort power off failed: %d\n", cleanup_err);
		}
	}
	return err;
}

static int frame_row(void *context, uint16_t y, uint8_t *packed_row)
{
	const uint8_t *frame = context;

	memcpy(packed_row, frame + (size_t)y * EPD3IN6E_ROW_BYTES, EPD3IN6E_ROW_BYTES);
	return 0;
}

int epd3in6e_refresh_frame(struct epd3in6e *display, const uint8_t *frame, size_t length)
{
	if (frame == NULL || length != EPD3IN6E_FRAME_BYTES) {
		return -EINVAL;
	}
	return epd3in6e_refresh(display, frame_row, (void *)frame);
}

int epd3in6e_sleep(struct epd3in6e *display)
{
	if (display == NULL) {
		return -EINVAL;
	}
	if (!display->initialized) {
		return -EACCES;
	}
	int err = deep_sleep(display);

	/* A failed transfer can still have put the panel to sleep. */
	display->initialized = false;
	return err;
}
