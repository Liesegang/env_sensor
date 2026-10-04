#ifndef OUTDOOR_SENSOR_CODEC_H_
#define OUTDOOR_SENSOR_CODEC_H_

#include <stddef.h>
#include <stdint.h>

uint8_t sensirion_crc8(const uint8_t *data, size_t length);
int sensirion_decode_words(const uint8_t *data, size_t length, uint16_t *words, size_t count);
uint16_t sgp41_temperature_ticks(int32_t temperature_mc);
uint16_t sgp41_humidity_ticks(int32_t humidity_mpercent);
int opt4001_decode_result(uint16_t msb, uint16_t lsb, uint32_t *millilux);

#endif
