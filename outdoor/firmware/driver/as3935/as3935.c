#include <as3935.h>

#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define AS3935_NODE DT_NODELABEL(as3935)

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(AS3935_NODE);
static bool initialized;

static int update_register(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t previous;
	int err = i2c_reg_read_byte_dt(&bus, reg, &previous);

	if (err != 0) {
		return err;
	}
	return i2c_reg_write_byte_dt(&bus, reg, (previous & ~mask) | (value & mask));
}

static int calibrate(void)
{
	int err = i2c_reg_write_byte_dt(&bus, 0x3d, 0x96);

	if (err == 0) {
		err = update_register(0x08, 0xe0, 0x40);
	}
	if (err == 0) {
		k_msleep(3);
		err = update_register(0x08, 0xe0, 0);
	} else {
		(void)update_register(0x08, 0xe0, 0);
	}
	if (err != 0) {
		return err;
	}
	uint8_t status[2];

	err = i2c_burst_read_dt(&bus, 0x3a, status, sizeof(status));
	if (err != 0) {
		return err;
	}
	return (status[0] & 0xc0) == 0x80 && (status[1] & 0xc0) == 0x80 ? 0 : -EIO;
}

int as3935_init(void)
{
	initialized = false;
	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	/* Preserve antenna tuning and reserved bits, including register 0x02 bit 7. */
	uint8_t gain = DT_PROP(AS3935_NODE, indoor) ? 0x12 : 0x0e;
	int err = update_register(0x00, 0x3f, gain << 1);

	if (err == 0) {
		err = update_register(0x01, 0x7f,
			(DT_PROP(AS3935_NODE, noise_floor_level) << 4) |
			DT_PROP(AS3935_NODE, watchdog_threshold));
	}
	static const uint8_t minimum_events[] = {1, 5, 9, 16};
	uint8_t minimum_index = 0;

	for (uint8_t i = 0; i < 4; i++) {
		if (minimum_events[i] == DT_PROP(AS3935_NODE, minimum_lightnings)) {
			minimum_index = i;
		}
	}
	if (err == 0) {
		err = update_register(0x02, 0x3f, (minimum_index << 4) |
				      DT_PROP(AS3935_NODE, spike_rejection));
	}
	if (err == 0) {
		err = update_register(0x03, 0x20,
			DT_PROP(AS3935_NODE, mask_disturbers) ? 0x20 : 0);
	}
#if DT_NODE_HAS_PROP(AS3935_NODE, tuning_capacitor)
	if (err == 0) {
		err = update_register(0x08, 0x0f, DT_PROP(AS3935_NODE, tuning_capacitor));
	}
#endif
	if (err == 0) {
		err = calibrate();
	}
	initialized = (err == 0);
	return err;
}

int as3935_read(struct as3935_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	/* With an IRQ connection, call after its rising edge in thread context. */
	k_msleep(2);
	uint8_t registers[9];
	int err = i2c_burst_read_dt(&bus, 0x00, registers, sizeof(registers));

	if (err != 0) {
		return err;
	}
	uint8_t calibration[2];

	err = i2c_burst_read_dt(&bus, 0x3a, calibration, sizeof(calibration));
	return err == 0 ? as3935_decode_registers(registers, calibration[0], calibration[1], sample) : err;
}

int as3935_calibrate(void)
{
	return initialized ? calibrate() : -EACCES;
}

int as3935_clear_statistics(void)
{
	if (!initialized) {
		return -EACCES;
	}
	int err = update_register(0x02, 0x40, 0x40);

	if (err == 0) {
		err = update_register(0x02, 0x40, 0);
	}
	return err == 0 ? update_register(0x02, 0x40, 0x40) : err;
}

int as3935_stop(void)
{
	if (!initialized) {
		return -EACCES;
	}
	int err = update_register(0x00, 0x01, 0x01);

	if (err == 0) {
		initialized = false;
	}
	return err;
}
