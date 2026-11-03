#include <sensor_codec.h>

#include <errno.h>

uint8_t sensirion_crc8(const uint8_t *data, size_t length)
{
	uint8_t crc = 0xff;

	for (size_t i = 0; i < length; i++) {
		crc ^= data[i];
		for (unsigned int bit = 0; bit < 8; bit++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
		}
	}
	return crc;
}

int sensirion_decode_words(const uint8_t *data, size_t length, uint16_t *words, size_t count)
{
	if (data == NULL || words == NULL || count == 0 || count > 6 || length != count * 3) {
		return -EINVAL;
	}
	/* Validate the whole packet before changing any caller-owned output. */
	for (size_t i = 0; i < count; i++) {
		if (sensirion_crc8(&data[i * 3], 2) != data[i * 3 + 2]) {
			return -EBADMSG;
		}
	}
	for (size_t i = 0; i < count; i++) {
		words[i] = ((uint16_t)data[i * 3] << 8) | data[i * 3 + 1];
	}
	return 0;
}

uint16_t sgp41_temperature_ticks(int32_t temperature_mc)
{
	if (temperature_mc < -45000) {
		temperature_mc = -45000;
	} else if (temperature_mc > 130000) {
		temperature_mc = 130000;
	}
	return (uint16_t)(((int64_t)(temperature_mc + 45000) * 65535 + 87500) / 175000);
}

uint16_t sgp41_humidity_ticks(int32_t humidity_mpercent)
{
	if (humidity_mpercent < 0) {
		humidity_mpercent = 0;
	} else if (humidity_mpercent > 100000) {
		humidity_mpercent = 100000;
	}
	return (uint16_t)(((int64_t)humidity_mpercent * 65535 + 50000) / 100000);
}

static uint8_t parity(uint32_t value)
{
	uint8_t result = 0;

	while (value != 0) {
		result ^= value & 1;
		value >>= 1;
	}
	return result;
}

int opt4001_decode_result(uint16_t msb, uint16_t lsb, uint32_t *millilux)
{
	uint8_t exponent = msb >> 12;
	uint32_t mantissa = ((uint32_t)(msb & 0x0fff) << 8) | (lsb >> 8);
	uint8_t counter = (lsb >> 4) & 0x0f;
	uint8_t crc = parity(mantissa) ^ parity(exponent) ^ parity(counter);

	if (millilux == NULL) {
		return -EINVAL;
	}
	/* TI datasheet, register 01: four independently defined parity bits. */
	crc |= (parity(mantissa & 0xaaaaa) ^ parity(exponent & 0x0a) ^
		parity(counter & 0x0a)) << 1;
	crc |= (parity(mantissa & 0x88888) ^ parity(exponent & 0x08) ^
		parity(counter & 0x08)) << 2;
	crc |= parity(mantissa & 0x80808) << 3;
	if (crc != (lsb & 0x0f)) {
		return -EBADMSG;
	}
	if (exponent > 8) {
		return -ERANGE;
	}
	/* DTS package: 437.5 micro-lux per count = 7/16 milli-lux. */
	*millilux = (uint32_t)((((uint64_t)mantissa << exponent) * 7 + 8) / 16);
	return 0;
}
