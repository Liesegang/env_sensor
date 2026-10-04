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

#endif
