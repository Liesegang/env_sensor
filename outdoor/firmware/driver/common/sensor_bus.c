#include <sensor_bus.h>
#include <sensor_codec.h>

#include <errno.h>
#include <zephyr/sys/byteorder.h>

int sensirion_write_command(const struct i2c_dt_spec *bus, uint16_t command)
{
	uint8_t bytes[2];

	sys_put_be16(command, bytes);
	return i2c_write_dt(bus, bytes, sizeof(bytes));
}

int sensirion_read_words(const struct i2c_dt_spec *bus, uint16_t *words, size_t count)
{
	uint8_t bytes[18];

	if (words == NULL || count == 0 || count > 6) {
		return -EINVAL;
	}
	int err = i2c_read_dt(bus, bytes, count * 3);

	return err == 0 ? sensirion_decode_words(bytes, count * 3, words, count) : err;
}
