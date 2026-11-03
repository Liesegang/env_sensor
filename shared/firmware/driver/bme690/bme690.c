#include <bme690.h>
#include <bme69x.h>

#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(bme690));
static bool initialized;
static struct bme69x_dev device;
static struct bme69x_conf configuration;

static int8_t bus_read(uint8_t reg, uint8_t *data, uint32_t length, void *context)
{
	const struct i2c_dt_spec *spec = context;

	return i2c_burst_read_dt(spec, reg, data, length) == 0 ? 0 : -1;
}

static int8_t bus_write(uint8_t reg, const uint8_t *data, uint32_t length, void *context)
{
	const struct i2c_dt_spec *spec = context;

	return i2c_burst_write_dt(spec, reg, data, length) == 0 ? 0 : -1;
}

static void delay_us(uint32_t duration, void *context)
{
	ARG_UNUSED(context);
	/* Round up instead of ending the conversion wait early. */
	k_msleep((duration + 999) / 1000);
}

static int api_error(int8_t result)
{
	if (result == BME69X_OK) {
		return 0;
	}
	if (result == BME69X_E_DEV_NOT_FOUND) {
		return -ENODEV;
	}
	return result == BME69X_W_NO_NEW_DATA ? -EAGAIN : -EIO;
}

int bme690_init(void)
{
	initialized = false;
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	device = (struct bme69x_dev){
		.intf = BME69X_I2C_INTF,
		.intf_ptr = (void *)&bus,
		.read = bus_read,
		.write = bus_write,
		.delay_us = delay_us,
		.amb_temp = 25,
	};
	int err = api_error(bme69x_init(&device));

	if (err != 0) {
		return err;
	}
	if (device.variant_id != BME690_VARIANT_GAS_HIGH) {
		return -ENOTSUP;
	}
	configuration = (struct bme69x_conf){
		.filter = BME69X_FILTER_OFF,
		.odr = BME69X_ODR_NONE,
		.os_hum = BME69X_OS_16X,
		.os_pres = BME69X_OS_1X,
		.os_temp = BME69X_OS_2X,
	};
	err = api_error(bme69x_set_conf(&configuration, &device));
	if (err != 0) {
		return err;
	}
	struct bme69x_heatr_conf heater = {
		.enable = BME69X_ENABLE,
		.heatr_temp = 300,
		.heatr_dur = 100,
	};

	err = api_error(bme69x_set_heatr_conf(BME69X_FORCED_MODE, &heater, &device));
	initialized = (err == 0);
	return err;
}

int bme690_read(struct bme690_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	int err = api_error(bme69x_set_op_mode(BME69X_FORCED_MODE, &device));

	if (err != 0) {
		return err;
	}
	delay_us(bme69x_get_meas_dur(BME69X_FORCED_MODE, &configuration, &device) + 100000,
		 NULL);
	struct bme69x_data data;
	uint8_t count = 0;

	err = api_error(bme69x_get_data(BME69X_FORCED_MODE, &data, &count, &device));
	if (err != 0) {
		return err;
	}
	if (count == 0 || !(data.status & BME69X_NEW_DATA_MSK)) {
		return -EAGAIN;
	}
	*sample = (struct bme690_sample){
		.temperature_mc = (int32_t)data.temperature * 10,
		.pressure_pa = data.pressure,
		.humidity_mpercent = data.humidity,
		.gas_resistance_ohm = data.gas_resistance,
		.chip_id = device.chip_id,
		.variant_id = device.variant_id,
		.status = data.status,
		.measurement_index = data.meas_index,
		.gas_index = data.gas_index,
		.heater_resistance = data.res_heat,
		.heater_current = data.idac,
		.gas_wait = data.gas_wait,
		.gas_valid = (data.status & BME69X_GASM_VALID_MSK) != 0,
		.heater_stable = (data.status & BME69X_HEAT_STAB_MSK) != 0,
	};
	device.amb_temp = data.temperature / 100;
	return 0;
}
