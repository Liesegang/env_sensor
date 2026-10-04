#include <opt4001.h>
#include <sensor_codec.h>

#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(opt4001));
static bool initialized;
static uint16_t device_id;

static int read_register(uint8_t reg, uint16_t *value)
{
	uint8_t bytes[2];
	int err = i2c_burst_read_dt(&bus, reg, bytes, sizeof(bytes));

	if (err == 0) {
		*value = sys_get_be16(bytes);
	}
	return err;
}

static int write_register(uint8_t reg, uint16_t value)
{
	uint8_t bytes[3] = {reg};

	sys_put_be16(value, &bytes[1]);
	return i2c_write_dt(&bus, bytes, sizeof(bytes));
}

int opt4001_init(void)
{
	initialized = false;
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	int err = read_register(0x11, &device_id);

	if (err != 0) {
		return err;
	}
	if ((device_id & 0x0fff) != 0x0121) {
		return -ENODEV;
	}
	/* Keep reserved bits at their mandated value and enable result burst reads. */
	err = write_register(0x0b, 0x8011);
	if (err == 0) {
		/* RANGE=12, CONVERSION_TIME=11, OPERATING_MODE=3, LATCH=1. */
		err = write_register(0x0a, 0x32f8);
	}
	initialized = (err == 0);
	return err;
}

int opt4001_read(struct opt4001_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	uint16_t status;
	int err = read_register(0x0c, &status);

	if (err != 0) {
		return err;
	}
	if (status & 0x08) {
		return -ERANGE;
	}
	if (!(status & 0x04)) {
		return -EAGAIN;
	}
	uint8_t bytes[4];

	err = i2c_burst_read_dt(&bus, 0x00, bytes, sizeof(bytes));
	if (err != 0) {
		return err;
	}
	uint16_t msb = sys_get_be16(bytes);
	uint16_t lsb = sys_get_be16(&bytes[2]);
	struct opt4001_sample value = {
		.device_id = device_id,
		.exponent = msb >> 12,
		.mantissa = ((uint32_t)(msb & 0x0fff) << 8) | (lsb >> 8),
		.counter = (lsb >> 4) & 0x0f,
	};

	err = opt4001_decode_result(msb, lsb, &value.millilux);
	if (err == 0) {
		*sample = value;
	}
	return err;
}
