#ifndef DISPLAY_TEST_EPD_H
#define DISPLAY_TEST_EPD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#define EPD3IN6E_WIDTH 400U
#define EPD3IN6E_HEIGHT 600U
#define EPD3IN6E_ROW_BYTES 200U
#define EPD3IN6E_FRAME_BYTES 120000U
#define EPD3IN6E_BLACK 0
#define EPD3IN6E_WHITE 1
#define EPD3IN6E_YELLOW 2
#define EPD3IN6E_RED 3
#define EPD3IN6E_BLUE 5
#define EPD3IN6E_GREEN 6
struct epd3in6e { struct spi_dt_spec spi;struct gpio_dt_spec dc,reset,busy;bool initialized; };
int epd3in6e_init(struct epd3in6e *d);
int epd3in6e_refresh_frame(struct epd3in6e*d,const uint8_t*frame,size_t length);
int epd3in6e_sleep(struct epd3in6e*d);
#endif
