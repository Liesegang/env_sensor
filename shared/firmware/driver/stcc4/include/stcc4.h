#ifndef OUTDOOR_STCC4_H_
#define OUTDOOR_STCC4_H_

#include <stdint.h>

struct stcc4_sample {
	uint64_t serial_number;
	uint32_t product_id;
	int32_t temperature_mc;
	int32_t humidity_mpercent;
	int16_t co2_ppm;
	uint16_t raw_temperature;
	uint16_t raw_humidity;
	uint16_t status;
};

/* Temperature and humidity come from an SHT4x on STCC4's auxiliary I2C bus. */
/* Wake, stop a previous session, read identity and start 1-Hz continuous mode. */
int stcc4_init(void);
/* As above, additionally run the 360-ms CRC-checked self-test before starting.
 * On success, *self_test contains the raw diagnostic word. Bit 4 indicates
 * that STCC4 did not detect the SHT4x on its auxiliary I2C bus; this is not
 * treated as an initialization error. The output is unchanged on error. */
int stcc4_init_with_diagnostic(uint16_t *self_test);
/* Read identity (and optionally diagnostics) without starting continuous mode,
 * then sleep. Keep VDD/VDDIO supplied to retain compensation and ASC state.
 */
int stcc4_init_single_shot(void);
int stcc4_init_single_shot_with_diagnostic(uint16_t *self_test);
/* Supply external T/RH from the same sensor when STCC4 has no SHT4x on its
 * auxiliary bus (self-test bit 4 set). Do not call while STCC4 controls an
 * attached SHT4x. Inputs are milli-degrees C and milli-percent RH.
 * In single-shot mode this wakes the sensor and leaves it idle until the next
 * stcc4_read_single_shot(), which returns it to sleep after measurement.
 */
int stcc4_set_rht_compensation(int32_t temperature_mc, int32_t humidity_mpercent);
/* Checks all four CRCs. An empty-buffer NACK is retried once after 150 ms;
 * returns -EAGAIN if still unavailable, without changing caller output. */
int stcc4_read(struct stcc4_sample *sample);
/* Continuous-mode ready polling for a fixed-phase MCU sampler. Issues one
 * read command, then retries only empty-buffer NACKs at <=5-ms intervals.
 * max_wait_ms is 0..200; nonzero includes the initial 1-ms execution wait.
 * CRC/transport errors return immediately, and output is unchanged on error.
 */
int stcc4_read_wait(struct stcc4_sample *sample, uint16_t max_wait_ms);
/* Wake, trigger a fresh 500-ms single shot, read all four CRC-checked words
 * (including auxiliary SHT4x T/RH), and sleep. If switching from continuous
 * mode, first stop it and drain old buffered data once. Recommended interval
 * 5..600 s; 60 s is supported. Output is unchanged on error, including failure
 * to enter sleep. Initial/long-idle accuracy and ASC warm-up still apply.
 */
int stcc4_read_single_shot(struct stcc4_sample *sample);
int stcc4_stop(void);

#endif
