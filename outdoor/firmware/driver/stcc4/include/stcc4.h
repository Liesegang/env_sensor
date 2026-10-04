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

/* Temperature and humidity come from the SHT45 on STCC4's auxiliary I2C bus. */
/* Wake, stop a previous session, read identity and start 1-Hz continuous mode. */
int stcc4_init(void);
/* Checks all four CRCs. An empty-buffer NACK is retried once after 150 ms;
 * returns -EAGAIN if still unavailable, without changing caller output. */
int stcc4_read(struct stcc4_sample *sample);
int stcc4_stop(void);

#endif
