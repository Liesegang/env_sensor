#include <stcc4.h>
#include <sensor_bus.h>
#include <sensor_codec.h>
#include <sht45.h>

#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(stcc4));
static bool initialized;
static bool continuous;
static bool sleeping;
static bool measurement_pending;
static uint32_t product_id;
static uint64_t serial_number;

static int send_wakeup(void)
{
	uint8_t wakeup = 0;
	int err = i2c_write_dt(&bus, &wakeup, 1);

	/* STCC4 D1 section 3.4.8: the 0x00 payload intentionally NACKs.
	 * Zephyr reports a NACK as -EIO; other transport errors still matter.
	 * The following ordinary command must acknowledge before proceeding.
	 */
	if (err != 0 && err != -EIO && err != -ENXIO) {
		return err;
	}
	k_msleep(5);
	sleeping = false;
	return 0;
}

static int enter_sleep(void)
{
	if (sleeping) {
		return 0;
	}
	int err = sensirion_write_command(&bus, 0x3650);

	if (err == 0) {
		k_msleep(1);
		sleeping = true;
	}
	return err;
}

static int stop_continuous(void)
{
	int err = sensirion_write_command(&bus, 0x3f86);

	if (err != 0) {
		return err;
	}
	k_msleep(1200);
	continuous = false;
	measurement_pending = true;
	return 0;
}

static int read_measurement_words(uint16_t words[4], bool retry_empty)
{
	int err = sensirion_write_command(&bus, 0xec05);

	if (err != 0) {
		return err;
	}
	k_msleep(1);
	err = sensirion_read_words(&bus, words, 4);
	if (err == -EIO && retry_empty) {
		/* D1 sections 3.4.1/3.4.3: retry an empty buffer once without
		 * reissuing the command; the continuous interval is 1 s +/-150 ms.
		 */
		k_msleep(150);
		err = sensirion_read_words(&bus, words, 4);
	}
	if (err == 0 || err == -EBADMSG || err == -EIO) {
		/* A complete read drains the buffer, including a bad-CRC packet.
		 * An empty-buffer NACK means there is nothing left to drain.
		 */
		measurement_pending = false;
	}
	return err == -EIO ? -EAGAIN : err;
}

static int discard_old_measurement(void)
{
	if (!measurement_pending) {
		return 0;
	}
	uint16_t words[4];
	int err = read_measurement_words(words, false);

	/* Old data is discarded so a later single shot cannot return it. */
	return err == -EAGAIN || err == -EBADMSG ? 0 : err;
}

static void decode_measurement(const uint16_t words[4], struct stcc4_sample *sample)
{
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
}

static int initialize(uint16_t *self_test, bool start_continuous)
{
	initialized = false;
	continuous = false;
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	int err = send_wakeup();

	if (err != 0) {
		return err;
	}
	err = stop_continuous();
	if (err != 0) {
		return err;
	}
	uint16_t diagnostic = 0;

	if (self_test != NULL) {
		/* This command is only accepted in idle mode, after the stop delay. */
		err = sensirion_write_command(&bus, 0x278c);
		if (err != 0) {
			return err;
		}
		/* Allow a small margin beyond the datasheet's 360-ms execution time. */
		k_msleep(370);
		err = sensirion_read_words(&bus, &diagnostic, 1);
		if (err != 0) {
			return err;
		}
	}
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
	if (start_continuous) {
		err = sensirion_write_command(&bus, 0x218b);
		continuous = err == 0;
	} else {
		err = discard_old_measurement();
		if (err == 0) {
			err = enter_sleep();
		}
	}
	initialized = (err == 0);
	if (err == 0 && self_test != NULL) {
		*self_test = diagnostic;
	}
	return err;
}

int stcc4_init(void)
{
	return initialize(NULL, true);
}

int stcc4_init_with_diagnostic(uint16_t *self_test)
{
	return self_test == NULL ? -EINVAL : initialize(self_test, true);
}

int stcc4_init_single_shot(void)
{
	return initialize(NULL, false);
}

int stcc4_init_single_shot_with_diagnostic(uint16_t *self_test)
{
	return self_test == NULL ? -EINVAL : initialize(self_test, false);
}

int stcc4_set_rht_compensation(int32_t temperature_mc, int32_t humidity_mpercent)
{
	if (!initialized) {
		return -EACCES;
	}
	if (sleeping) {
		int err = send_wakeup();

		if (err != 0) {
			return err;
		}
	}
	/* STCC4 uses the SHT4x raw-word scale. Its RH encoding differs from
	 * the SGP4x compensation scale: 50 %RH is 0x72b0, not 0x8000. */
	if (temperature_mc < -45000) {
		temperature_mc = -45000;
	} else if (temperature_mc > 130000) {
		temperature_mc = 130000;
	}
	if (humidity_mpercent < 0) {
		humidity_mpercent = 0;
	} else if (humidity_mpercent > 100000) {
		humidity_mpercent = 100000;
	}
	uint16_t raw_temperature = (uint16_t)(((int64_t)(temperature_mc + 45000) * 65535 +
						 87500) / 175000);
	uint16_t raw_humidity = (uint16_t)(((int64_t)(humidity_mpercent + 6000) * 65535 +
					    62500) / 125000);
	uint8_t command[8] = {0xe0, 0x00};

	sys_put_be16(raw_temperature, &command[2]);
	command[4] = sensirion_crc8(&command[2], 2);
	sys_put_be16(raw_humidity, &command[5]);
	command[7] = sensirion_crc8(&command[5], 2);
	int err = i2c_write_dt(&bus, command, sizeof(command));

	if (err == 0) {
		k_msleep(1);
	}
	return err;
}

int stcc4_read(struct stcc4_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized || !continuous) {
		return -EACCES;
	}
	uint16_t words[4];
	int err = read_measurement_words(words, true);

	if (err != 0) {
		return err;
	}
	decode_measurement(words, sample);
	return 0;
}

int stcc4_read_wait(struct stcc4_sample *sample, uint16_t max_wait_ms)
{
	if (sample == NULL || max_wait_ms > 200) { return -EINVAL; }
	if (!initialized || !continuous) { return -EACCES; }
	int64_t deadline = k_uptime_get() + max_wait_ms;
	int err = sensirion_write_command(&bus, 0xec05);
	if (err != 0) { return err; }
	k_msleep(1);
	uint16_t words[4];
	for (;;) {
		err = sensirion_read_words(&bus, words, 4);
		if (err != -EIO) { break; }
		/* D1 §3.4.3 permits subsequent read-address transactions and NACKs
		 * an empty buffer. Never reissue the command or reuse old output. */
		int64_t remaining = deadline - k_uptime_get();
		if (remaining <= 0) { break; }
		k_msleep((int32_t)(remaining < 5 ? remaining : 5));
	}
	if (err == 0 || err == -EBADMSG || err == -EIO) { measurement_pending = false; }
	if (err == 0) { decode_measurement(words, sample); }
	return err == -EIO ? -EAGAIN : err;
}

int stcc4_read_single_shot(struct stcc4_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	int err = 0;

	/* A legacy continuous session is stopped only when changing modes. */
	if (continuous) {
		err = stop_continuous();
		if (err != 0) {
			return err;
		}
	}
	if (sleeping) {
		err = send_wakeup();
	}
	if (err == 0) {
		err = discard_old_measurement();
	}
	if (err == 0) {
		err = sensirion_write_command(&bus, 0x219d);
		if (err == 0) {
			measurement_pending = true;
			/* D1 section 3.4.6: one fresh measurement takes 500 ms. */
			k_msleep(510);
		}
	}
	uint16_t words[4];

	if (err == 0) {
		err = read_measurement_words(words, true);
	}
	/* Sleep retains compensation and ASC state (D1 section 3.4.7).
	 * Keep the rail supplied: repeated power cycling restarts initial ASC.
	 */
	int sleep_err = enter_sleep();

	if (err != 0) {
		return err;
	}
	if (sleep_err != 0) {
		return sleep_err;
	}
	decode_measurement(words, sample);
	return 0;
}

int stcc4_stop(void)
{
	if (!initialized) {
		return -EACCES;
	}
	int err = sleeping ? 0 : stop_continuous();

	if (err == 0) {
		initialized = false;
	}
	return err;
}
