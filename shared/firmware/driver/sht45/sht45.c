#include <sht45.h>

int32_t sht45_temperature_mc(uint16_t raw)
{
	return -45000 + (int32_t)((int64_t)175000 * raw / 65535);
}

int32_t sht45_humidity_mpercent(uint16_t raw)
{
	int32_t value = -6000 + (int32_t)((int64_t)125000 * raw / 65535);

	return value < 0 ? 0 : (value > 100000 ? 100000 : value);
}
