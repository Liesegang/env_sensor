#ifndef OUTDOOR_SGP41_H_
#define OUTDOOR_SGP41_H_

#include <stdint.h>
#include <stdbool.h>

struct sgp41_sample {
	uint64_t serial_number; /* 48 significant bits */
	uint16_t raw_voc;
	uint16_t raw_nox;
	uint16_t self_test;
	uint16_t temperature_ticks;
	uint16_t humidity_ticks;
	bool nox_valid;
	bool conditioning;
	uint16_t voc_index; /* Sensirion VOC Index: 1..500; 0 during blackout. */
	uint16_t nox_index; /* Sensirion NOx Index: 1..500; 0 during blackout. */
	bool voc_index_valid;
	bool nox_index_valid;
};

/* Read serial number and run the manufacturer's self-test. */
int sgp41_init(void);
/* CRC-checked raw signals + indices from the official 1-Hz Gas Index Algorithm. */
/* First 10 seconds condition NOx; each algorithm then needs >45 valid samples. */
/* During conditioning, nox_valid is false. Blocks for at most a 60-ms conversion. */
int sgp41_read(int32_t temperature_mc, int32_t humidity_mpercent, struct sgp41_sample *sample);
int sgp41_stop(void);

#endif
