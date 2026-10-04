#include <sgp41.h>
#include <sensor_bus.h>
#include <sensor_codec.h>
#include "gas_indices.h"

#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(sgp41));
static bool initialized;
static uint64_t serial_number;
static uint16_t self_test;
static int64_t conditioning_deadline;

int sgp41_init(void)
{
	initialized = false;
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	/* Put a prior measurement session in idle before restarting conditioning. */
	int err = sensirion_write_command(&bus, 0x3615);

	if (err != 0) {
		return err;
	}
	k_msleep(1);
	err = sensirion_write_command(&bus, 0x3682);
	uint16_t words[3];

	if (err != 0) {
		return err;
	}
	k_msleep(1);
	err = sensirion_read_words(&bus, words, 3);
	if (err != 0) {
		return err;
	}
	serial_number = ((uint64_t)words[0] << 32) | ((uint64_t)words[1] << 16) | words[2];
	err = sensirion_write_command(&bus, 0x280e);
	if (err != 0) {
		return err;
	}
	/* Datasheet maximum is 320 ms; shorter waits can produce a NACK. */
	k_msleep(330);
	err = sensirion_read_words(&bus, &self_test, 1);
	if (err != 0) {
		return err;
	}
	/* The low byte of the successful D4xx pattern is unspecified. */
	if ((self_test & 0xff00) != 0xd400) {
		return -EIO;
	}
	initialized = true;
	conditioning_deadline = 0;
	sgp41_indices_reset();
	return 0;
}

int sgp41_read(int32_t temperature_mc, int32_t humidity_mpercent, struct sgp41_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	struct sgp41_sample value = {
		.serial_number = serial_number,
		.self_test = self_test,
		.temperature_ticks = sgp41_temperature_ticks(temperature_mc),
		.humidity_ticks = sgp41_humidity_ticks(humidity_mpercent),
	};
	if (conditioning_deadline == 0) {
		conditioning_deadline = k_uptime_get() + 10000;
	}
	value.conditioning = k_uptime_get() < conditioning_deadline;
	value.nox_valid = !value.conditioning;
	/* Conditioning uses the manufacturer's default T/RH parameters. */
	if (value.conditioning) {
		value.temperature_ticks = 0x6666;
		value.humidity_ticks = 0x8000;
	}
	uint8_t bytes[8] = {0x26, value.conditioning ? 0x12 : 0x19};

	sys_put_be16(value.humidity_ticks, &bytes[2]);
	bytes[4] = sensirion_crc8(&bytes[2], 2);
	sys_put_be16(value.temperature_ticks, &bytes[5]);
	bytes[7] = sensirion_crc8(&bytes[5], 2);
	int err = i2c_write_dt(&bus, bytes, sizeof(bytes));

	if (err != 0) {
		return err;
	}
	k_msleep(60);
	uint16_t words[2];

	err = sensirion_read_words(&bus, words, value.conditioning ? 1 : 2);
	if (err == 0) {
		value.raw_voc = words[0];
		value.raw_nox = value.conditioning ? 0 : words[1];
		sgp41_indices_process(&value, k_uptime_get());
		*sample = value;
	}
	return err;
}

int sgp41_stop(void)
{
	if (!initialized) {
		return -EACCES;
	}
	int err = sensirion_write_command(&bus, 0x3615);

	if (err == 0) {
		k_msleep(1);
		initialized = false;
	}
	return err;
}
