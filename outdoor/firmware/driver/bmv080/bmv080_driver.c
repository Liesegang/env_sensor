#include <bmv080_driver.h>

#include <errno.h>
#include <stddef.h>

#if defined(CONFIG_APP_BMV080)
#include <bmv080.h>
#include <math.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(bmv080));
static bmv080_handle_t handle;
static bool initialized;
static int last_bus_error;
static int current_error;
static int64_t received_ms;
static uint32_t last_read_sequence;
static struct bmv080_sample latest;
static uint8_t transfer_buffer[4098];
static struct k_thread worker;
K_THREAD_STACK_DEFINE(worker_stack, 16384);
K_MUTEX_DEFINE(sample_mutex);

static int8_t read_words(bmv080_sercom_handle_t context, uint16_t header,
			uint16_t *payload, uint16_t count)
{
	(void)context;
	if (payload == NULL || count == 0) {
		last_bus_error = -EINVAL;
		return -1;
	}
	/* I2C addresses are word addresses. A read requires STOP after the header. */
	sys_put_be16((uint16_t)(header << 1), transfer_buffer);
	last_bus_error = i2c_write_dt(&bus, transfer_buffer, 2);
	for (uint32_t offset = 0; last_bus_error == 0 && offset < count; ) {
		uint16_t words = MIN((uint32_t)count - offset, 128U);

		/* The BMV080 permits subsequent reads without another header. */
		last_bus_error = i2c_read_dt(&bus, transfer_buffer, words * 2);
		if (last_bus_error == 0) {
			for (uint16_t i = 0; i < words; i++) {
				payload[offset + i] = sys_get_be16(&transfer_buffer[i * 2]);
			}
			offset += words;
		}
	}
	return last_bus_error == 0 ? 0 : -1;
}

static int8_t write_words(bmv080_sercom_handle_t context, uint16_t header,
			 const uint16_t *payload, uint16_t count)
{
	(void)context;
	if ((payload == NULL && count != 0) || count > 2048) {
		last_bus_error = -EMSGSIZE;
		return -1;
	}
	sys_put_be16((uint16_t)(header << 1), transfer_buffer);
	for (uint16_t i = 0; i < count; i++) {
		sys_put_be16(payload[i], &transfer_buffer[2 + i * 2]);
	}
	last_bus_error = i2c_write_dt(&bus, transfer_buffer, 2 + count * 2);
	return last_bus_error == 0 ? 0 : -1;
}

static int8_t delay_ms(uint32_t duration)
{
	k_msleep(duration);
	return 0;
}

static int sdk_error(bmv080_status_code_t status)
{
	if (status == E_BMV080_OK) {
		return 0;
	}
	printk("ERROR: BMV080 SDK status=%d I2C=%d\n", (int)status, last_bus_error);
	return last_bus_error != 0 ? last_bus_error : -EIO;
}

static bool to_milli(float value, int32_t *result)
{
	if (!isfinite(value) || value < 0 || value > 2147483.0f) {
		return false;
	}
	*result = (int32_t)(value * 1000.0f);
	return true;
}

static void data_ready(bmv080_output_t output, void *context)
{
	(void)context;
	struct bmv080_sample sample = {
		.is_obstructed = output.is_obstructed,
		.is_outside_measurement_range = output.is_outside_measurement_range,
	};
	double runtime_ms = (double)output.runtime_in_sec * 1000.0;
	bool valid = isfinite(output.runtime_in_sec) && output.runtime_in_sec >= 0 &&
		     runtime_ms < 18446744073709551616.0 &&
		     to_milli(output.pm1_mass_concentration, &sample.pm1_mass_milliug_m3) &&
		     to_milli(output.pm2_5_mass_concentration, &sample.pm2_5_mass_milliug_m3) &&
		     to_milli(output.pm10_mass_concentration, &sample.pm10_mass_milliug_m3) &&
		     to_milli(output.pm1_number_concentration, &sample.pm1_number_milliparticles_cm3) &&
		     to_milli(output.pm2_5_number_concentration, &sample.pm2_5_number_milliparticles_cm3) &&
		     to_milli(output.pm10_number_concentration, &sample.pm10_number_milliparticles_cm3);

	k_mutex_lock(&sample_mutex, K_FOREVER);
	if (valid) {
		sample.runtime_ms = (uint64_t)runtime_ms;
		sample.sequence = latest.sequence + 1;
		latest = sample;
		received_ms = k_uptime_get();
		current_error = 0;
	} else {
		current_error = -EBADMSG;
	}
	k_mutex_unlock(&sample_mutex);
}

static void poll_sensor(void *unused1, void *unused2, void *unused3)
{
	(void)unused1;
	(void)unused2;
	(void)unused3;
	int previous_status = 0;

	for (;;) {
		last_bus_error = 0;
		bmv080_status_code_t status = bmv080_serve_interrupt(handle, data_ready, NULL);

		if (status != E_BMV080_OK) {
			k_mutex_lock(&sample_mutex, K_FOREVER);
			current_error = last_bus_error != 0 ? last_bus_error : -EIO;
			k_mutex_unlock(&sample_mutex);
			if ((int)status != previous_status) {
				(void)sdk_error(status);
			}
		}
		previous_status = status;
		k_msleep(status == E_BMV080_OK ? 10 : 100);
	}
}

int bmv080_driver_init(void)
{
	if (initialized) {
		return -EALREADY;
	}
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	/* Leave enough transfer-time margin for the SDK's burst writes. */
	int err = i2c_configure(bus.bus, I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_FAST));

	if (err != 0) {
		return err;
	}
	err = sdk_error(bmv080_open(&handle, (void *)&bus, read_words, write_words, delay_ms));

	if (err != 0) {
		return err;
	}
	err = sdk_error(bmv080_reset(handle));
	char sensor_id[13] = {0};

	if (err == 0) {
		err = sdk_error(bmv080_get_sensor_id(handle, sensor_id));
	}
	if (err == 0) {
		printk("BMV080: sensor_id=%s\n", sensor_id);
		err = sdk_error(bmv080_start_continuous_measurement(handle));
	}
	if (err != 0) {
		(void)bmv080_close(&handle);
		return err;
	}
	initialized = true;
	k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
			poll_sensor, NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	return 0;
}

int bmv080_driver_read(struct bmv080_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	k_mutex_lock(&sample_mutex, K_FOREVER);
	int err = current_error;

	if (err == 0 && latest.sequence == 0) {
		err = -EAGAIN;
	}
	if (err == 0) {
		*sample = latest;
		sample->age_ms = (uint32_t)(k_uptime_get() - received_ms);
		sample->fresh = sample->sequence != last_read_sequence;
		last_read_sequence = sample->sequence;
	}
	k_mutex_unlock(&sample_mutex);
	return err;
}

#else

int bmv080_driver_init(void)
{
	return -ENOSYS;
}

int bmv080_driver_read(struct bmv080_sample *sample)
{
	return sample == NULL ? -EINVAL : -ENOSYS;
}

#endif
