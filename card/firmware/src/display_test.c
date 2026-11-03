/*
 * Render the supplied Card dashboard layout with its simulated 24-hour data.
 * Drawing belongs to epd_dashboard; controller/SPI handling belongs to epd3in6e.
 * The frame is static BSS so rendering does not consume the thread stack.
 */
#include "display_test.h"

#include <epd3in6e.h>
#include <epd_dashboard.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static struct epd3in6e display = {
	.spi = SPI_DT_SPEC_GET(DT_NODELABEL(epd_display), SPI_WORD_SET(8), 0),
	.dc = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_dc_gpios),
	.reset = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_reset_gpios),
	.busy = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_busy_gpios),
};
static uint8_t frame[EPD3IN6E_FRAME_BYTES];
static struct k_thread display_thread;
K_THREAD_STACK_DEFINE(display_stack, 4096);
static bool started;

static void display_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	printk("DISPLAY TEST: render supplied dashboard layout with synthetic data\n");
	printk("DISPLAY TEST: 24 hours, 1440 samples, 8 sensors; values are not live measurements\n");
	printk("DISPLAY TEST: HCHO is simulated too; the aldehyde sensor is not installed\n");
	int err = epd_dashboard_demo_render(frame, sizeof(frame));

	if (err == 0) {
		printk("DISPLAY TEST: dashboard rendered; initialize 3.6inch Spectra 6 on SPI20\n");
		err = epd3in6e_init(&display);
	}
	if (err == 0) {
		printk("DISPLAY TEST: refresh once, 600 x 400 landscape dashboard (120000 bytes)\n");
		err = epd3in6e_refresh_frame(&display, frame, sizeof(frame));
		if (err == 0) {
			printk("DISPLAY TEST: dashboard refresh completed\n");
		}
	}
	int sleep_err = 0;

	if (display.initialized) {
		sleep_err = epd3in6e_sleep(&display);
		if (sleep_err == 0) {
			printk("DISPLAY TEST: deep sleep\n");
		} else {
			printk("DISPLAY TEST: deep sleep failed: %d\n", sleep_err);
		}
	}
	if (err == 0) {
		err = sleep_err;
	}
	if (err == 0) {
		printk("DISPLAY TEST: finished; visually check sample graph and eight value cards\n");
	} else {
		printk("DISPLAY TEST: FAIL: %d\n", err);
	}
}

int display_test_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_create(&display_thread, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
			display_worker, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	return 0;
}
