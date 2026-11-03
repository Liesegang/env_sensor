#include <opt4001.h>
#include <sensor_codec.h>

#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#define OPT4001_CONFIG_CONTINUOUS_800MS 0x32f8
#define OPT4001_CONFIG_POWER_DOWN_100MS 0x3208
#define OPT4001_CONFIG_FORCED_SHOT_100MS 0x3218
#define OPT4001_SINGLE_SHOT_WAIT_MS 128
#define OPT4001_SINGLE_SHOT_TIMEOUT_MS 300

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(opt4001));
static bool initialized;
static uint16_t device_id;
static uint16_t configuration;

static int read_bytes(uint8_t reg, uint8_t *bytes, size_t size)
{
	/* OPT4001 requires STOP after the register pointer write. */
	int err = i2c_write_dt(&bus, &reg, sizeof(reg));

	if (err != 0) {
		return err;
	}
	return i2c_read_dt(&bus, bytes, size);
}

static int read_register(uint8_t reg, uint16_t *value)
{
	uint8_t bytes[2];
	int err = read_bytes(reg, bytes, sizeof(bytes));

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

static int initialize_with_config(uint16_t config)
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
		err = write_register(0x0a, config);
	}
	initialized = (err == 0);
	if (initialized) {
		configuration = config;
	}
	return err;
}

int opt4001_init(void)
{
	/* Preserve Outdoor continuous auto-range, 800-ms behavior. */
	return initialize_with_config(OPT4001_CONFIG_CONTINUOUS_800MS);
}

int opt4001_init_low_power(void)
{
	/* TI OPT4001 datasheet sections 8.4.1/8.6.1.11: RANGE=12, CT=8
	 * (100 ms), OPERATING_MODE=0, LATCH=1, QWAKE=0 for full standby.
	 */
	return initialize_with_config(OPT4001_CONFIG_POWER_DOWN_100MS);
}

int opt4001_power_down(void)
{
	if (!initialized) {
		return -EACCES;
	}
	/* OPERATING_MODE bits 5:4 = 0, QWAKE bit 15 = 0. */
	uint16_t config = configuration & (uint16_t)~0x8030U;
	int err = write_register(0x0a, config);

	if (err == 0) {
		configuration = config;
	}
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

	err = read_bytes(0x00, bytes, sizeof(bytes));
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

int opt4001_read_single_shot(struct opt4001_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	/* Reading status clears CONVERSION_READY, so an old result cannot satisfy
	 * this trigger. Preserve the STOP-separated pointer/read transactions.
	 */
	uint16_t previous_status;
	int err = read_register(0x0c, &previous_status);
	struct opt4001_sample value;

	if (err == 0) {
		err = write_register(0x0a, OPT4001_CONFIG_FORCED_SHOT_100MS);
	}
	if (err == 0) {
		configuration = OPT4001_CONFIG_FORCED_SHOT_100MS;
		int64_t deadline = k_uptime_get() + OPT4001_SINGLE_SHOT_TIMEOUT_MS;

		/* Mode 1 restarts auto-range for infrequent measurements. TI specifies
		 * 100-ms conversion plus ~0.5-ms wake/~0.5-ms auto-range reset; 128 ms
		 * is a sleeping wait with margin, not a device conversion-time setting.
		 */
		k_msleep(OPT4001_SINGLE_SHOT_WAIT_MS);
		while (true) {
			if (k_uptime_get() >= deadline) {
				err = -ETIMEDOUT;
				break;
			}
			err = opt4001_read(&value);
			if (err != -EAGAIN) {
				break;
			}
			int64_t remaining = deadline - k_uptime_get();

			if (remaining > 0) {
				k_msleep(remaining < 10 ? remaining : 10);
			}
		}
	}
	/* Mode 1 normally self-clears to standby after conversion. Explicitly
	 * restore full power-down as well when triggering or reading fails.
	 */
	int power_down_err = opt4001_power_down();

	if (err == 0) {
		err = power_down_err;
	}
	if (err == 0) {
		*sample = value;
	}
	return err;
}
