#ifndef OUTDOOR_BMV080_DRIVER_H_
#define OUTDOOR_BMV080_DRIVER_H_

#include <stdbool.h>
#include <stdint.h>

struct bmv080_sample {
	int32_t pm1_mass_milliug_m3;
	int32_t pm2_5_mass_milliug_m3;
	int32_t pm10_mass_milliug_m3;
	int32_t pm1_number_milliparticles_cm3;
	int32_t pm2_5_number_milliparticles_cm3;
	int32_t pm10_number_milliparticles_cm3;
	uint64_t runtime_ms;
	uint32_t sequence;
	uint32_t age_ms;
	bool is_obstructed;
	bool is_outside_measurement_range;
	bool fresh;
};

/* Requires the official Bosch SDK. Returns -ENOSYS when it is not configured. */
int bmv080_driver_init(void);
/* Snapshot of the last SDK output; -EAGAIN before the first result. */
int bmv080_driver_read(struct bmv080_sample *sample);

#endif
