/* SPDX-License-Identifier: MIT */
#ifndef EPD3IN6E_H_
#define EPD3IN6E_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPD3IN6E_WIDTH 400U
#define EPD3IN6E_HEIGHT 600U
#define EPD3IN6E_ROW_BYTES (EPD3IN6E_WIDTH / 2U)
#define EPD3IN6E_FRAME_BYTES (EPD3IN6E_ROW_BYTES * EPD3IN6E_HEIGHT)

enum epd3in6e_color {
	EPD3IN6E_BLACK = 0x0,
	EPD3IN6E_WHITE = 0x1,
	EPD3IN6E_YELLOW = 0x2,
	EPD3IN6E_RED = 0x3,
	EPD3IN6E_BLUE = 0x5,
	EPD3IN6E_GREEN = 0x6,
};

struct epd3in6e {
	/* Mode 0, MSB first, 8-bit words; GPIO CS is managed by SPI. */
	struct spi_dt_spec spi;
	struct gpio_dt_spec dc;
	/* reset and busy must both be GPIO_ACTIVE_LOW. */
	struct gpio_dt_spec reset;
	struct gpio_dt_spec busy;
	bool initialized;
};

/*
 * Fill exactly EPD3IN6E_ROW_BYTES for native row y (0..599).
 * Each byte stores its left pixel in the high nibble and right pixel in the
 * low nibble. Return zero or a negative errno; only the six colors are valid.
 * The buffer belongs to the driver and is reused after the callback returns.
 */
typedef int (*epd3in6e_row_fn)(void *context, uint16_t y, uint8_t *packed_row);

/* Board supply must already be on and stable. Reset and write panel registers. */
int epd3in6e_init(struct epd3in6e *display);

/*
 * Stream a frame with a 200-byte row buffer, refresh, then power off the panel
 * internally. Waits up to 60 seconds per BUSY phase. Board supply stays on.
 * Success confirms the BUSY response, not the visible image. On failure,
 * reinitialize before another refresh. Serialize calls to the same display.
 */
int epd3in6e_refresh(struct epd3in6e *display, epd3in6e_row_fn row_fn, void *context);

/* Full native 400x600 packed frame; length must equal EPD3IN6E_FRAME_BYTES. */
int epd3in6e_refresh_frame(struct epd3in6e *display, const uint8_t *frame, size_t length);

/* Enter deep sleep. Call init again to wake; board supply is caller-controlled. */
int epd3in6e_sleep(struct epd3in6e *display);

#ifdef __cplusplus
}
#endif

#endif /* EPD3IN6E_H_ */
