#include <sfa40.h>
#include <sensor_bus.h>

#include <errno.h>
#include <zephyr/kernel.h>

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(sfa40));
static bool initialized;
static uint64_t serial_number;
static uint32_t product_id;
static int64_t first_sample_ms;

int sfa40_init(void)
{
	initialized = false;
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	/* Commands contain their own CRC byte; do not substitute SFA30 commands. */
	int err = sensirion_write_command(&bus, 0x50d2);

	if (err != 0) {
		return err;
	}
	/* Match the manufacturer's current embedded driver stop delay. */
	k_msleep(700);
	err = sensirion_write_command(&bus, 0x03ff);
	uint16_t words[3];

	if (err != 0) {
		return err;
	}
	k_msleep(1);
	err = sensirion_read_words(&bus, words, 2);
	if (err != 0) {
		return err;
	}
	product_id = ((uint32_t)words[0] << 16) | words[1];
	err = sensirion_write_command(&bus, 0x02ce);
	if (err != 0) {
		return err;
	}
	k_msleep(1);
	err = sensirion_read_words(&bus, words, 3);
	if (err != 0) {
		return err;
	}
	serial_number = ((uint64_t)words[0] << 32) | ((uint64_t)words[1] << 16) | words[2];
	err = sensirion_write_command(&bus, 0x00ac);
	if (err == 0) {
		first_sample_ms = k_uptime_get() + 500;
		initialized = true;
	}
	return err;
}

int sfa40_read(struct sfa40_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	if (k_uptime_get() < first_sample_ms) {
		return -EAGAIN;
	}
	int err = sensirion_write_command(&bus, 0xc0eb);

	if (err != 0) {
		return err;
	}
	k_msleep(1);
	uint16_t words[4];

	err = sensirion_read_words(&bus, words, 4);
	if (err != 0) {
		return err;
	}
	struct sfa40_sample value;

	err = sfa40_decode_words(words, &value);
	if (err == 0) {
		value.serial_number = serial_number;
		value.product_id = product_id;
		*sample = value;
	}
	return err;
}

int sfa40_stop(void)
{
	if (!initialized) {
		return -EACCES;
	}
	int err = sensirion_write_command(&bus, 0x50d2);

	if (err == 0) {
		k_msleep(700);
		initialized = false;
	}
	return err;
}
