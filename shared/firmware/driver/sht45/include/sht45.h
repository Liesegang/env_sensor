#ifndef OUTDOOR_SHT45_H_
#define OUTDOOR_SHT45_H_

#include <stdint.h>

/* Decode the SHT45 words returned by STCC4, which owns the auxiliary bus. */
int32_t sht45_temperature_mc(uint16_t raw);
int32_t sht45_humidity_mpercent(uint16_t raw);

#endif
