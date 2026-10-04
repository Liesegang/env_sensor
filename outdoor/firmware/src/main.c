#include <buzzer.h>
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
#include <telemetry.h>
#if defined(CONFIG_APP_SD_LOG)
#include <sd_log.h>
#endif
#endif
#if defined(CONFIG_APP_SENSOR_READ)
#include <bme690.h>
#if defined(CONFIG_APP_BMV080)
#include <bmv080_driver.h>
#endif
#if defined(CONFIG_APP_AS3935)
#include <as3935.h>
#endif
#if defined(CONFIG_APP_SFA40)
#include <sfa40.h>
#endif
#include <opt4001.h>
#include <sgp41.h>
#include <stcc4.h>
#endif

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_APP_I2C_SCAN)
static int scan_i2c(void)
{
	const struct device *bus = DEVICE_DT_GET(DT_ALIAS(i2c_sensors));
	unsigned int found = 0;
	uint8_t unused = 0;
	struct i2c_msg probe = {
		.buf = &unused,
		.len = 0,
		.flags = I2C_MSG_WRITE | I2C_MSG_STOP,
	};

	if (!device_is_ready(bus)) {
		printk("ERROR: I2C controller is not ready\n");
		return -ENODEV;
	}

	/* Allow the powered sensors to finish startup before probing. */
	k_msleep(1000);
	int recovery = i2c_recover_bus(bus);

	if (recovery != 0) {
		const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));

		printk("ERROR: I2C bus recovery: %d; SCL=%d SDA=%d\n", recovery,
		       gpio_pin_get(gpio, 3), gpio_pin_get(gpio, 4));
		return recovery;
	}
	printk("I2C SCAN START: %s, SCL P0.03, SDA P0.04, %u Hz, 7-bit 0x01..0x03 + 0x08..0x77\n",
	       bus->name, DT_PROP(DT_ALIAS(i2c_sensors), clock_frequency));
	for (uint16_t address = 0x01; address <= 0x77; address++) {
		/* AS3935 uses 0x01..0x03; skip general call and other reserved IDs. */
		if (address > 0x03 && address < 0x08) {
			continue;
		}
		/* Address + STOP only: no sensor command or register data. */
		int err = i2c_transfer(bus, &probe, 1, address);

		if (err == 0) {
			printk("I2C FOUND: 0x%02x\n", address);
			found++;
		} else if (err != -EIO && err != -ENXIO) {
			printk("ERROR: I2C probe 0x%02x failed: %d; scan aborted\n", address, err);
			return err;
		}
		k_msleep(2);
	}
	printk("I2C SCAN DONE: %u device(s)\n", found);
	return 0;
}
#endif

#if defined(CONFIG_APP_SENSOR_READ)
static int init_errors[7];
static bool opt_ready;
static bool sgp_ready;
static bool stcc_ready;
static bool bme_ready;
#if defined(CONFIG_APP_BMV080)
static bool bmv_ready;
#endif
#if defined(CONFIG_APP_AS3935)
static bool lightning_ready;
#endif
#if defined(CONFIG_APP_SFA40)
static bool hcho_ready;
#endif

static bool report_init(const char *name, int err)
{
	if (err != 0) {
		printk("ERROR: %s init failed: %d\n", name, err);
		return false;
	}
	printk("%s READY\n", name);
	return true;
}

static void print_milli(int32_t value)
{
	uint32_t magnitude = value < 0 ? (uint32_t)-(int64_t)value : (uint32_t)value;

	printk("%s%u.%03u", value < 0 ? "-" : "", magnitude / 1000, magnitude % 1000);
}

static void read_sensors(uint32_t sequence)
{
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
	struct telemetry_sample frame = {
		.sequence = sequence,
		.uptime_ms = k_uptime_get(),
		.enabled_mask = 0x0f,
		.opt4001.error = init_errors[0],
		.bme690.error = init_errors[1],
		.sgp41.error = init_errors[2],
		.stcc4.error = init_errors[3],
	};
#if defined(CONFIG_APP_BMV080)
	frame.enabled_mask |= 0x10;
	frame.bmv080.error = init_errors[4];
#endif
#if defined(CONFIG_APP_AS3935)
	frame.enabled_mask |= 0x20;
	frame.as3935.error = init_errors[5];
#endif
#if defined(CONFIG_APP_SFA40)
	frame.enabled_mask |= 0x40;
	frame.sfa40.error = init_errors[6];
#endif
#else
	ARG_UNUSED(sequence);
#endif
	int32_t compensation_temp = 25000;
	int32_t compensation_rh = 50000;
	const char *compensation_source = "default";
	int err;

	if (opt_ready) {
		struct opt4001_sample sample;

		err = opt4001_read(&sample);
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
		frame.opt4001.error = err;
		if (err == 0) { frame.opt4001.value = sample; }
#endif
		if (err == 0) {
			printk("OPT4001: lux=");
			print_milli(sample.millilux);
			printk(" mantissa=%u exponent=%u counter=%u id=0x%04x\n",
			       sample.mantissa, sample.exponent, sample.counter, sample.device_id);
		} else {
			printk("ERROR: OPT4001 read: %d\n", err);
		}
	}
	if (bme_ready) {
		struct bme690_sample sample;

		err = bme690_read(&sample);
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
		frame.bme690.error = err;
		if (err == 0) { frame.bme690.value = sample; }
#endif
		if (err == 0) {
			compensation_temp = sample.temperature_mc;
			compensation_rh = sample.humidity_mpercent;
			compensation_source = "BME690";
			printk("BME690: temp_C=");
			print_milli(sample.temperature_mc);
			printk(" rh_pct=");
			print_milli(sample.humidity_mpercent);
			printk(" pressure_Pa=%u gas_ohm=%u gas_valid=%u heater_stable=%u "
			       "status=0x%02x meas=%u gas_index=%u res_heat=%u idac=%u "
			       "gas_wait=%u chip=0x%02x variant=%u\n",
			       sample.pressure_pa, sample.gas_resistance_ohm, sample.gas_valid,
			       sample.heater_stable, sample.status, sample.measurement_index,
			       sample.gas_index, sample.heater_resistance, sample.heater_current,
			       sample.gas_wait, sample.chip_id, sample.variant_id);
		} else {
			printk("ERROR: BME690 read: %d\n", err);
		}
	}
	if (stcc_ready) {
		struct stcc4_sample sample;

		err = stcc4_read(&sample);
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
		frame.stcc4.error = err;
		if (err == 0) { frame.stcc4.value = sample; }
#endif
		if (err == 0) {
			compensation_temp = sample.temperature_mc;
			compensation_rh = sample.humidity_mpercent;
			compensation_source = "STCC4/SHT45";
			printk("STCC4: co2_ppm=%d status=0x%04x product=0x%08x serial=%016llx\n",
			       sample.co2_ppm, sample.status, sample.product_id,
			       (unsigned long long)sample.serial_number);
			printk("SHT45: via=STCC4 temp_C=");
			print_milli(sample.temperature_mc);
			printk(" rh_pct=");
			print_milli(sample.humidity_mpercent);
			printk(" raw_t=%u raw_rh=%u\n", sample.raw_temperature, sample.raw_humidity);
		} else {
			printk("ERROR: STCC4 read: %d\n", err);
		}
	}
	if (sgp_ready) {
		struct sgp41_sample sample;

		err = sgp41_read(compensation_temp, compensation_rh, &sample);
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
		frame.sgp41.error = err;
		if (err == 0) { frame.sgp41.value = sample; }
#endif
		if (err == 0) {
			printk("SGP41: raw_voc=%u raw_nox=%u nox_valid=%u conditioning=%u "
			       "voc_index=%u voc_index_valid=%u nox_index=%u nox_index_valid=%u "
			       "comp=%s temp_ticks=%u rh_ticks=%u "
			       "self_test=0x%04x serial=%012llx\n",
			       sample.raw_voc, sample.raw_nox, sample.nox_valid, sample.conditioning,
			       sample.voc_index, sample.voc_index_valid, sample.nox_index, sample.nox_index_valid,
			       sample.conditioning ? "default" : compensation_source, sample.temperature_ticks,
			       sample.humidity_ticks, sample.self_test,
			       (unsigned long long)sample.serial_number);
		} else {
			printk("ERROR: SGP41 read: %d\n", err);
		}
	}
#if defined(CONFIG_APP_BMV080)
	if (bmv_ready) {
		struct bmv080_sample sample;

		err = bmv080_driver_read(&sample);
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
		frame.bmv080.error = err;
		if (err == 0) { frame.bmv080.value = sample; }
#endif
		if (err == 0) {
			printk("BMV080: pm1_ug_m3=");
			print_milli(sample.pm1_mass_milliug_m3);
			printk(" pm2_5_ug_m3=");
			print_milli(sample.pm2_5_mass_milliug_m3);
			printk(" pm10_ug_m3=");
			print_milli(sample.pm10_mass_milliug_m3);
			printk(" pm1_particles_cm3=");
			print_milli(sample.pm1_number_milliparticles_cm3);
			printk(" pm2_5_particles_cm3=");
			print_milli(sample.pm2_5_number_milliparticles_cm3);
			printk(" pm10_particles_cm3=");
			print_milli(sample.pm10_number_milliparticles_cm3);
			printk(" obstructed=%u outside_range=%u runtime_ms=%llu seq=%u "
			       "age_ms=%u fresh=%u\n", sample.is_obstructed,
			       sample.is_outside_measurement_range, (unsigned long long)sample.runtime_ms,
			       sample.sequence, sample.age_ms, sample.fresh);
		} else if (err == -EAGAIN) {
			printk("BMV080: waiting for first SDK output\n");
		} else {
			printk("ERROR: BMV080 read: %d\n", err);
		}
	}
#endif
#if defined(CONFIG_APP_SFA40)
	if (hcho_ready) {
		struct sfa40_sample sample;

		err = sfa40_read(&sample);
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
		frame.sfa40.error = err;
		if (err == 0) { frame.sfa40.value = sample; }
#endif
		if (err == 0) {
			printk("SFA40: hcho_ppb=");
			print_milli(sample.hcho_millippb);
			printk(" temp_C=");
			print_milli(sample.temperature_mc);
			printk(" rh_pct=");
			print_milli(sample.humidity_mpercent);
			printk(" ready=%u within_spec=%u status=0x%02x raw_hcho=%u "
			       "raw_t=%u raw_rh=%u product=0x%08x serial=%012llx\n",
			       sample.ready, sample.within_specification, sample.status,
			       sample.raw_hcho, sample.raw_temperature, sample.raw_humidity,
			       sample.product_id, (unsigned long long)sample.serial_number);
		} else {
			printk("ERROR: SFA40 read: %d\n", err);
		}
	}
#endif
#if defined(CONFIG_APP_AS3935)
	if (lightning_ready) {
		struct as3935_sample sample;

		err = as3935_read(&sample);
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
		frame.as3935.error = err;
		if (err == 0) { frame.as3935.value = sample; }
#endif
		if (err == 0) {
			printk("AS3935: irq=0x%02x lightning=%u noise_high=%u disturber=%u "
			       "energy=%u distance_km=%d distance_code=%u valid=%u overhead=%u "
			       "out_of_range=%u indoor=%u powered_down=%u noise_floor=%u "
			       "watchdog=%u spike_rejection=%u minimum_lightnings=%u "
			       "mask_disturbers=%u tuning_cap=%u trco=0x%02x srco=0x%02x\n",
			       sample.interrupt_reason, sample.lightning, sample.noise_high,
			       sample.disturber, sample.energy, sample.distance_km,
			       sample.distance_code, sample.distance_valid, sample.storm_overhead,
			       sample.out_of_range, sample.indoor, sample.powered_down,
			       sample.noise_floor, sample.watchdog_threshold, sample.spike_rejection,
			       sample.minimum_lightnings, sample.disturbers_masked,
			       sample.tuning_capacitor, sample.trco_calibration, sample.srco_calibration);
		} else {
			printk("ERROR: AS3935 read: %d\n", err);
		}
	}
#endif
#if defined(CONFIG_APP_BLE) || defined(CONFIG_APP_SD_LOG)
#if defined(CONFIG_APP_BLE)
	telemetry_ble_publish(&frame);
#endif
#if defined(CONFIG_APP_SD_LOG)
	sd_log_submit(&frame);
#endif
#endif
}

static void monitor_sensors(void)
{
	opt_ready = report_init("OPT4001", init_errors[0] = opt4001_init());
	bme_ready = report_init("BME690", init_errors[1] = bme690_init());
	sgp_ready = report_init("SGP41", init_errors[2] = sgp41_init());
	stcc_ready = report_init("STCC4", init_errors[3] = stcc4_init());
#if defined(CONFIG_APP_BMV080)
	bmv_ready = report_init("BMV080", init_errors[4] = bmv080_driver_init());
#endif
#if defined(CONFIG_APP_SFA40)
	hcho_ready = report_init("SFA40", init_errors[6] = sfa40_init());
#endif
#if defined(CONFIG_APP_AS3935)
	lightning_ready = report_init("AS3935", init_errors[5] = as3935_init());
#endif
	/* STCC4 needs its first full continuous-measurement interval. */
	k_msleep(1100);
	int64_t first_ms = k_uptime_get();
	int64_t next_ms = first_ms;

	for (unsigned int sequence = 1; ; sequence++) {
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
		printk("SAMPLE %u at %lld ms\n", sequence,
		       (long long)(k_uptime_get() - first_ms));
		read_sensors(sequence);
		next_ms += 1000;
		int64_t now = k_uptime_get();

		if (next_ms <= now) {
			/* Skip expired slots instead of issuing measurements back-to-back. */
			int64_t missed = (now - next_ms) / 1000 + 1;

			next_ms += missed * 1000;
			printk("SAMPLE OVERRUN: skipped %lld interval(s)\n", (long long)missed);
		}
	}
}
#endif

#if defined(CONFIG_APP_BUZZER_TEST)
static int test_buzzer(void)
{
	printk("Buzzer timing: on %u ms, interval %u ms, count %u, startup %u ms\n",
	       CONFIG_BUZZER_ON_MS, CONFIG_BUZZER_INTERVAL_MS, CONFIG_BUZZER_BEEP_COUNT,
	       CONFIG_BUZZER_STARTUP_DELAY_MS);
	k_msleep(CONFIG_BUZZER_STARTUP_DELAY_MS);

	int64_t first_beep_ms = k_uptime_get();

	for (unsigned int i = 0; i < CONFIG_BUZZER_BEEP_COUNT; i++) {
		k_sleep(K_TIMEOUT_ABS_MS(first_beep_ms + (int64_t)i * CONFIG_BUZZER_INTERVAL_MS));
		int err = buzzer_start();
		if (err != 0) {
			printk("ERROR: cannot start buzzer: %d\n", err);
			return err;
		}
		printk("BUZZER ON %u/%u at %lld ms\n", i + 1, CONFIG_BUZZER_BEEP_COUNT,
		       (long long)(k_uptime_get() - first_beep_ms));
		k_msleep(CONFIG_BUZZER_ON_MS);

		err = buzzer_stop();
		if (err != 0) {
			printk("ERROR: cannot silence buzzer: %d\n", err);
			return err;
		}
		printk("BUZZER OFF %u/%u\n", i + 1, CONFIG_BUZZER_BEEP_COUNT);
	}
	printk("BUZZER DONE\n");
	return 0;
}
#endif

int main(void)
{
	int err = buzzer_init();

	if (err == 0) {
		err = buzzer_stop();
	}
	if (err != 0) {
		printk("ERROR: cannot silence buzzer: %d\n", err);
		return 0;
	}
	printk("BUZZER STOPPED\n");

#if defined(CONFIG_APP_BLE)
	err = telemetry_ble_init();
	if (err != 0) { printk("ERROR: BLE init: %d\n", err); }
#endif
#if defined(CONFIG_APP_I2C_SCAN)
	(void)scan_i2c();
#endif
#if defined(CONFIG_APP_BUZZER_TEST)
	(void)test_buzzer();
#endif
#if defined(CONFIG_APP_SENSOR_READ)
	monitor_sensors();
#endif
	return 0;
}
