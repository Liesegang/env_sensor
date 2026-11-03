#ifndef OUTDOOR_OPT4001_H_
#define OUTDOOR_OPT4001_H_

#include <stdint.h>

struct opt4001_sample {
	uint32_t millilux;
	uint32_t mantissa;
	uint16_t device_id;
	uint8_t exponent;
	uint8_t counter;
};

/* Initialize continuous auto-range, 800-ms conversions. Zero or negative errno. */
int opt4001_init(void);
/* Return a fresh CRC-checked sample, or -EAGAIN if no conversion has finished. */
int opt4001_read(struct opt4001_sample *sample);

/* Initialize auto-range with 100-ms conversions, QWAKE=0, in power-down mode. */
int opt4001_init_low_power(void);
/*
 * Trigger forced auto-range, 100-ms one-shot conversion, sleep for 128 ms and
 * wait at most 300 ms total for a fresh result. Finish in power-down even on
 * conversion/read errors. Calls must be serialized with other OPT4001 calls.
 */
int opt4001_read_single_shot(struct opt4001_sample *sample);
/* Stop conversions and disable quick-wake, retaining range/conversion timing. */
int opt4001_power_down(void);

#endif
