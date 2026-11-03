#ifndef OUTDOOR_SENSOR_BUS_H_
#define OUTDOOR_SENSOR_BUS_H_

#include <zephyr/drivers/i2c.h>

int sensirion_write_command(const struct i2c_dt_spec *bus, uint16_t command);
int sensirion_read_words(const struct i2c_dt_spec *bus, uint16_t *words, size_t count);

#endif
