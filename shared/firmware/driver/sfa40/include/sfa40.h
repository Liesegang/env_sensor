#ifndef OUTDOOR_SFA40_H_
#define OUTDOOR_SFA40_H_

#include <stdbool.h>
#include <stdint.h>

struct sfa40_sample {
	uint64_t serial_number;
	uint32_t product_id;
	int32_t hcho_millippb;
	int32_t humidity_mpercent;
	int32_t temperature_mc;
	uint16_t raw_hcho;
	uint16_t raw_humidity;
	uint16_t raw_temperature;
	uint8_t status;
	bool ready;
	bool within_specification;
};

/* Initialize continuous measurements; read returns -EAGAIN for the first 500 ms. */
int sfa40_init(void);
int sfa40_read(struct sfa40_sample *sample);
int sfa40_stop(void);
/* Decode CRC-validated words. The status is in the high byte of words[3]. */
int sfa40_decode_words(const uint16_t words[4], struct sfa40_sample *sample);

#endif
