#include <stcc4.h>
#include <sensor_bus.h>
#include <sensor_codec.h>
#include <sht45.h>

#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(stcc4));
static bool initialized;
static uint32_t product_id;
static uint64_t serial_number;

int stcc4_init(void)
{
	initialized = false;
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	uint8_t wakeup = 0;

	/* The wakeup transaction may NACK while still waking the sensor. */
	(void)i2c_write_dt(&bus, &wakeup, 1);
	k_msleep(5);
	int err = sensirion_write_command(&bus, 0x3f86);

	if (err != 0) {
		return err;
	}
	k_msleep(1200);
	err = sensirion_write_command(&bus, 0x365b);
	if (err != 0) {
		return err;
	}
	k_msleep(1);
	uint16_t words[6];

	err = sensirion_read_words(&bus, words, 6);
	if (err != 0) {
		return err;
	}
	product_id = ((uint32_t)words[0] << 16) | words[1];
	serial_number = ((uint64_t)words[2] << 48) | ((uint64_t)words[3] << 32) |
			((uint64_t)words[4] << 16) | words[5];
	err = sensirion_write_command(&bus, 0x218b);
	initialized = (err == 0);
	return err;
}

int stcc4_read(struct stcc4_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	int err = sensirion_write_command(&bus, 0xec05);

	if (err != 0) {
		return err;
	}
	k_msleep(1);
	uint16_t words[4];

	err = sensirion_read_words(&bus, words, 4);
	if (err == -EIO) {
		/* STCC4 D1, sections 3.4.1/3.4.3: an empty buffer NACKs;
		 * the sensor's 1-s interval has +/-150-ms clock tolerance.
		 * Retry the read once, without issuing another command. */
		k_msleep(150);
		err = sensirion_read_words(&bus, words, 4);
		if (err == -EIO) {
			return -EAGAIN;
		}
	}
	if (err != 0) {
		return err;
	}
	*sample = (struct stcc4_sample){
		.serial_number = serial_number,
		.product_id = product_id,
		.co2_ppm = (int16_t)words[0],
		.raw_temperature = words[1],
		.raw_humidity = words[2],
		.temperature_mc = sht45_temperature_mc(words[1]),
		.humidity_mpercent = sht45_humidity_mpercent(words[2]),
		.status = words[3],
	};
	return 0;
}

int stcc4_stop(void)
{
	if (!initialized) {
		return -EACCES;
	}
	int err = sensirion_write_command(&bus, 0x3f86);

	if (err == 0) {
		k_msleep(1200);
		initialized = false;
	}
	return err;
}
