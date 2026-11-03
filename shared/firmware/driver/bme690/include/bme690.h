#ifndef OUTDOOR_BME690_H_
#define OUTDOOR_BME690_H_

#include <stdbool.h>
#include <stdint.h>

struct bme690_sample {
	int32_t temperature_mc;
	uint32_t pressure_pa;
	uint32_t humidity_mpercent;
	uint32_t gas_resistance_ohm;
	uint8_t chip_id;
	uint8_t variant_id;
	uint8_t status;
	uint8_t measurement_index;
	uint8_t gas_index;
	uint8_t heater_resistance;
	uint8_t heater_current;
	uint8_t gas_wait;
	bool gas_valid;
	bool heater_stable;
};

/* Bosch Sensor API compensation; 300-degree heater for 100 ms in forced mode. */
int bme690_init(void);
/* Trigger and wait for one compensated measurement. Zero or negative errno. */
int bme690_read(struct bme690_sample *sample);

#endif
