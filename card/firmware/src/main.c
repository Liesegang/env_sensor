#include <bme690.h>
#include <opt4001.h>
#include <sgp41.h>
#include <stcc4.h>
#include <sfa40.h>

#include "sd_logger.h"
#include "card_sample.h"
#include "card_ble.h"
#include "card_history.h"
#include "switch_test.h"
#include "pmic.h"
#include "card_display.h"
#include "co2_alarm.h"
#include "card_cadence.h"
#include "card_image_transfer.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>

static const struct gpio_dt_spec buzzer_drive =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), buzzer_drive_gpios);
static const struct gpio_dt_spec sensor_power =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), sensor_power_gpios);
static const struct gpio_dt_spec display_cs =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_cs_gpios);
static const struct gpio_dt_spec display_reset =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_reset_gpios);

static atomic_t opt_ready;
static atomic_t bme_ready;
static atomic_t sgp_ready;
static atomic_t stcc_ready;
static bool stcc_has_sht;
static bool stcc_needs_external_rht;
static uint16_t stcc_self_test;
static atomic_t sfa_ready;
static struct opt4001_sample last_opt;
static struct bme690_sample last_bme;
static struct stcc4_sample last_stcc;
static struct sgp41_sample last_sgp;
static struct sfa40_sample last_sfa;
static bool sfa_sample_available;
static bool display_ready;
static bool alarm_ready;

struct card_snapshot {
	struct card_sample sample;
	struct opt4001_sample opt;
	struct bme690_sample bme;
	struct stcc4_sample stcc;
	struct sgp41_sample sgp;
	struct sfa40_sample sfa;
	bool sfa_available;
	int64_t started_ms;
};
static struct card_snapshot latest_snapshot;
K_MUTEX_DEFINE(snapshot_lock);
K_SEM_DEFINE(acquisition_start, 0, 1);
K_SEM_DEFINE(first_sample_available, 0, 1);

BUILD_ASSERT(DT_GPIO_PIN(DT_PATH(zephyr_user), buzzer_drive_gpios) == 1,
	     "The v2 buzzer drive is on P1.01");
BUILD_ASSERT(DT_GPIO_FLAGS(DT_PATH(zephyr_user), buzzer_drive_gpios) == GPIO_ACTIVE_HIGH,
	     "The v2 buzzer drive is active-high");
BUILD_ASSERT(DT_GPIO_PIN(DT_PATH(zephyr_user), sensor_power_gpios) == 0,
	     "The v2 +V enable is on P1.00");
BUILD_ASSERT(DT_GPIO_FLAGS(DT_PATH(zephyr_user), sensor_power_gpios) == GPIO_ACTIVE_LOW,
	     "The v2 +V enable is active-low");

static void print_milli(int32_t value)
{
	uint32_t magnitude = value < 0 ? (uint32_t)-(int64_t)value : (uint32_t)value;

	printk("%s%u.%03u", value < 0 ? "-" : "", magnitude / 1000, magnitude % 1000);
}

static bool report_init(const char *name, int err)
{
	if (err != 0) {
		printk("ERROR: %s init failed: %d\n", name, err);
		return false;
	}
	printk("%s READY\n", name);
	return true;
}

static int power_on_sensors(void)
{
	if (!gpio_is_ready_dt(&sensor_power) || !gpio_is_ready_dt(&buzzer_drive) ||
	    !gpio_is_ready_dt(&display_cs) || !gpio_is_ready_dt(&display_reset)) {
		return -ENODEV;
	}
	/* Hold the switched rail off until the buzzer pin is driven low. */
	int err = gpio_pin_configure_dt(&sensor_power, GPIO_OUTPUT_INACTIVE);

	if (err != 0) {
		return err;
	}
	err = gpio_pin_configure_dt(&buzzer_drive, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}
	/* Hold the panel deselected and in reset before its BUCK1 supply starts. */
	err = gpio_pin_configure_dt(&display_cs, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}
	err = gpio_pin_configure_dt(&display_reset, GPIO_OUTPUT_ACTIVE);
	if (err != 0) {
		return err;
	}
	err = gpio_pin_set_dt(&sensor_power, 1);
	if (err == 0) {
		printk("+V ON; buzzer drive held low\n");
	}
	return err;
}

static void initialize_stcc(void)
{
	int err = stcc4_init_with_diagnostic(&stcc_self_test);

	if (err != 0) {
		printk("ERROR: STCC4 diagnostic: %d; trying normal continuous init\n", err);
		err = stcc4_init();
		stcc_has_sht = false;
		stcc_needs_external_rht = false;
	} else {
		stcc_has_sht = stcc_self_test == 0;
		stcc_needs_external_rht = (stcc_self_test & BIT(4)) != 0;
		printk("STCC4 self-test=0x%04x SHT45=%s\n", stcc_self_test,
		       stcc_has_sht ? "connected" : "unavailable/not validated");
		/* Supply, core I2C and memory faults invalidate CO2 measurements.
		 * A missing auxiliary SHT4x (bit 4) can use external compensation.
		 */
		if (stcc_self_test & (BIT(0) | BIT(3) | BIT(5) | BIT(6))) {
			printk("ERROR: STCC4 diagnostic fault; CO2 remains missing\n");
			err = -EIO;
		}
	}
	atomic_set(&stcc_ready, report_init("STCC4 continuous 1Hz", err));
}

static void initialize_sensors(void)
{
	atomic_set(&opt_ready, report_init("OPT4001 continuous", opt4001_init()));
	atomic_set(&bme_ready, report_init("BME690 1Hz forced", bme690_init()));
	atomic_set(&sgp_ready, report_init("SGP41 continuous 1Hz/indices", sgp41_init()));
	initialize_stcc();
	/* Absent SFA40 is expected; detect it again at the next minute. */
	atomic_set(&sfa_ready, sfa40_init() == 0);
	printk("SFA40 %s\n", atomic_get(&sfa_ready) ? "READY" : "not installed/unavailable");
}

static void read_sensors(struct card_sample *sample)
{
	int32_t compensation_temp = 25000, compensation_rh = 50000;
	int err;

	sfa_sample_available = false;

	if (atomic_get(&bme_ready)) {
		err = bme690_read(&last_bme);
		if (err == 0) {
			sample->bme_temperature_mc = last_bme.temperature_mc;
			sample->bme_humidity_mpercent = last_bme.humidity_mpercent;
			sample->temperature_mc = last_bme.temperature_mc;
			sample->humidity_mpercent = last_bme.humidity_mpercent;
			sample->pressure_pa = last_bme.pressure_pa;
			sample->gas_resistance_ohm = last_bme.gas_resistance_ohm;
			sample->gas_valid = last_bme.gas_valid;
			sample->heater_stable = last_bme.heater_stable;
			sample->rht_source = CARD_RHT_BME690;
			sample->valid |= CARD_VALID_TEMPERATURE | CARD_VALID_HUMIDITY |
				CARD_VALID_BME_TEMPERATURE | CARD_VALID_BME_HUMIDITY |
				CARD_VALID_PRESSURE;
			if (last_bme.gas_valid) {
				sample->valid |= CARD_VALID_BME_GAS;
			}
			compensation_temp = last_bme.temperature_mc;
			compensation_rh = last_bme.humidity_mpercent;
			printk("BME690: temp_C=");
			print_milli(last_bme.temperature_mc);
			printk(" rh_pct=");
			print_milli(last_bme.humidity_mpercent);
			printk(" pressure_Pa=%u gas_ohm=%u\n", last_bme.pressure_pa,
			       last_bme.gas_resistance_ohm);
		} else {
			printk("ERROR: BME690 read: %d\n", err);
			if (err != -EAGAIN) { atomic_clear(&bme_ready); }
		}
	}
	if (atomic_get(&stcc_ready)) {
		if (stcc_needs_external_rht) {
			err = stcc4_set_rht_compensation(compensation_temp, compensation_rh);
			if (err != 0) {
				printk("ERROR: STCC4 RHT compensation: %d\n", err);
			}
		}
		err = stcc4_read_wait(&last_stcc, 200);
		if (err == 0) {
			sample->co2_ppm = last_stcc.co2_ppm;
			sample->stcc_status = last_stcc.status;
			sample->stcc_self_test = stcc_self_test;
			if (last_stcc.co2_ppm >= 0) {
				sample->valid |= CARD_VALID_CO2;
			}
			printk("STCC4: co2_ppm=%d status=0x%04x continuous\n",
			       last_stcc.co2_ppm, last_stcc.status);
			if (stcc_has_sht) {
				sample->temperature_mc = last_stcc.temperature_mc;
				sample->humidity_mpercent = last_stcc.humidity_mpercent;
				sample->rht_source = CARD_RHT_SHT45;
				sample->valid |= CARD_VALID_TEMPERATURE | CARD_VALID_HUMIDITY;
				compensation_temp = last_stcc.temperature_mc;
				compensation_rh = last_stcc.humidity_mpercent;
				printk("SHT45: via=STCC4 temp_C=");
				print_milli(last_stcc.temperature_mc);
				printk(" rh_pct=");
				print_milli(last_stcc.humidity_mpercent);
				printk("\n");
			}
		} else {
			printk("ERROR: STCC4 read: %d\n", err);
			if (err != -EAGAIN) { atomic_clear(&stcc_ready); }
		}
	}
	if (atomic_get(&opt_ready)) {
		err = opt4001_read(&last_opt);
		if (err == 0) {
			sample->millilux = last_opt.millilux;
			sample->valid |= CARD_VALID_LUX;
			printk("OPT4001: lux=%u.%03u continuous\n",
			       last_opt.millilux / 1000, last_opt.millilux % 1000);
		} else {
			printk("ERROR: OPT4001 read: %d\n", err);
			if (err != -EAGAIN) { atomic_clear(&opt_ready); }
		}
	}
	if (atomic_get(&sfa_ready)) {
		err = sfa40_read(&last_sfa);
		if (err == 0) {
			sfa_sample_available = true;
			if (last_sfa.ready && last_sfa.within_specification) {
				sample->hcho_millippb = last_sfa.hcho_millippb;
				sample->valid |= CARD_VALID_HCHO;
			}
			printk("SFA40: hcho_millippb=%d ready=%u within_spec=%u\n",
			       last_sfa.hcho_millippb, last_sfa.ready,
			       last_sfa.within_specification);
		} else {
			printk("ERROR: SFA40 read: %d\n", err);
			if (err != -EAGAIN) { atomic_clear(&sfa_ready); }
		}
	}
	if (atomic_get(&sgp_ready)) {
		err = sgp41_read(compensation_temp, compensation_rh, &last_sgp);
		if (err == 0) {
			sample->raw_voc = last_sgp.raw_voc;
			sample->raw_nox = last_sgp.raw_nox;
			sample->valid |= CARD_VALID_VOC_RAW;
			if (last_sgp.nox_valid) {
				sample->valid |= CARD_VALID_NOX_RAW;
			}
			sample->voc_index = last_sgp.voc_index;
			sample->nox_index = last_sgp.nox_index;
			if (last_sgp.voc_index_valid) { sample->valid |= CARD_VALID_VOC_INDEX; }
			if (last_sgp.nox_index_valid) { sample->valid |= CARD_VALID_NOX_INDEX; }
			printk("SGP41: raw_voc=%u raw_nox=%u voc_index=%u/%u nox_index=%u/%u\n",
			       last_sgp.raw_voc, last_sgp.raw_nox,
			       last_sgp.voc_index, last_sgp.voc_index_valid,
			       last_sgp.nox_index, last_sgp.nox_index_valid);
		} else {
			printk("ERROR: SGP41 read: %d\n", err);
			if (err != -EAGAIN) { atomic_clear(&sgp_ready); }
		}
	}
}

/* Recover one unavailable driver from main while the 1-Hz thread continues
 * reading the other devices. Ready is published only after init completes. */
static void recover_sensors(void)
{
	if (!atomic_get(&opt_ready)) { atomic_set(&opt_ready, report_init("OPT4001 retry", opt4001_init())); }
	if (!atomic_get(&bme_ready)) { atomic_set(&bme_ready, report_init("BME690 retry", bme690_init())); }
	if (!atomic_get(&sgp_ready)) { atomic_set(&sgp_ready, report_init("SGP41 retry", sgp41_init())); }
	if (!atomic_get(&stcc_ready)) { initialize_stcc(); }
	if (!atomic_get(&sfa_ready)) { atomic_set(&sfa_ready, sfa40_init() == 0); }
}

static void acquire_samples(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3);
	k_sem_take(&acquisition_start, K_FOREVER);
	int64_t next_sample = k_uptime_get();
	uint32_t dropped_rows = 0;
	for (uint32_t sequence = 1; ; ++sequence) {
		k_sleep(K_TIMEOUT_ABS_MS(next_sample));
		int64_t started_ms = k_uptime_get();
		struct card_sample sample = {
			.sequence = sequence,
			.uptime_s = (uint32_t)(started_ms / 1000),
			.unix_s = card_ble_unix_seconds(started_ms),
		};
		read_sensors(&sample);
		if (alarm_ready) {
			co2_alarm_evaluate((sample.valid & CARD_VALID_CO2) != 0, sample.co2_ppm);
		}
		int error = sd_logger_queue(&sample);
		if (error) {
			++dropped_rows;
			if (dropped_rows == 1 || dropped_rows % 60 == 0) {
				printk("ERROR: SD queue=%d dropped_rows=%u; acquisition continues\n", error, dropped_rows);
			}
		}
		if (display_ready) { card_display_record(&sample); }
		k_mutex_lock(&snapshot_lock, K_FOREVER);
		latest_snapshot = (struct card_snapshot){
			.sample = sample, .opt = last_opt, .bme = last_bme,
			.stcc = last_stcc, .sgp = last_sgp, .sfa = last_sfa,
			.sfa_available = sfa_sample_available, .started_ms = started_ms,
		};
		k_mutex_unlock(&snapshot_lock);
		if (sequence == 1) { k_sem_give(&first_sample_available); }
		int64_t completed_ms = k_uptime_get();
		printk("SAMPLE %u at %lld ms duration=%lld ms valid=0x%04x\n",
		       sequence, (long long)started_ms, (long long)(completed_ms - started_ms), sample.valid);
		int64_t advanced = card_cadence_advance(next_sample, completed_ms, CARD_SAMPLE_PERIOD_MS);
		if (advanced - next_sample > CARD_SAMPLE_PERIOD_MS) {
			printk("ERROR: sample deadline missed; skipped=%u\n",
			       (unsigned int)((advanced - next_sample) / CARD_SAMPLE_PERIOD_MS - 1));
		}
		next_sample = advanced;
	}
}
K_THREAD_DEFINE(card_acquisition, 4096, acquire_samples, NULL, NULL, NULL, 5, 0, 0);

/* A short PUSH toggles screens on release. A long pairing hold never starts
 * an unnecessary graph/image refresh in front of the upcoming PIN screen. */
static void handle_switch(enum card_switch_contact contact, bool pressed,
                          uint32_t count, void *context)
{
	static int64_t push_pressed_ms = -1;
	if (contact != CARD_SWITCH_PUSH) {
		card_display_button(contact, pressed, count, context);
	} else if (pressed) {
		push_pressed_ms = k_uptime_get();
	} else if (push_pressed_ms >= 0) {
		int64_t held_ms = k_uptime_get() - push_pressed_ms;
		push_pressed_ms = -1;
		if (held_ms < 3000 && !card_ble_pairing_button_consumed()) {
			card_display_button(CARD_SWITCH_PUSH, true, count, context);
		}
	}
}

static void request_display(void)
{
	if (!display_ready) { return; }
	int error = card_display_refresh_periodic();
	if (error && error != -EALREADY && error != -EBUSY) {
		printk("ERROR: display request: %d\n", error);
	}
}

static void publish_snapshot(void)
{
	struct card_snapshot snapshot;
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	snapshot = latest_snapshot;
	k_mutex_unlock(&snapshot_lock);
	int error = card_ble_publish(&snapshot.sample,
		(snapshot.sample.valid & CARD_VALID_LUX) ? &snapshot.opt : NULL,
		(snapshot.sample.valid & CARD_VALID_PRESSURE) ? &snapshot.bme : NULL,
		(snapshot.sample.valid & CARD_VALID_VOC_RAW) ? &snapshot.sgp : NULL,
		(snapshot.sample.valid & CARD_VALID_CO2) ? &snapshot.stcc : NULL,
		snapshot.sfa_available ? &snapshot.sfa : NULL);
	if (error) { printk("ERROR: BLE publish: %d\n", error); }
}

int main(void)
{
	int err = power_on_sensors();
	if (err != 0) { printk("ERROR: power/buzzer setup: %d\n", err); return err; }
	alarm_ready = report_init("CO2 alarm", co2_alarm_init());
	k_msleep(1000);
	/* Device runtime PM only releases idle bus/PWM clocks. System PM and
	 * System OFF are disabled; sensor heaters/continuous sessions remain on. */
	err = pm_device_runtime_enable(DEVICE_DT_GET(DT_NODELABEL(i2c30)));
	if (err) { printk("ERROR: I2C runtime PM: %d\n", err); }
	err = pm_device_runtime_enable(DEVICE_DT_GET(DT_NODELABEL(spi20)));
	if (err) { printk("ERROR: SPI runtime PM: %d\n", err); }
	err = pmic_init();
	display_ready = err == 0;
	if (err) { printk("ERROR: nPM1300 setup: %d\n", err); }
	err = pmic_charger_enable(4200, 32);
	if (err) { printk("ERROR: charger enable: %d\n", err); }
	k_msleep(2000);
	(void)pmic_log_status();
	initialize_sensors();
	/* The first continuous STCC4/OPT conversion needs a full interval. */
	k_msleep(1100);
	if (display_ready) { display_ready = card_display_init(true) == 0; }
	if (display_ready) { switch_test_set_handler(handle_switch, NULL); }
	err = switch_test_start();
	if (err) { printk("ERROR: switch setup: %d\n", err); }
	bool ble_ready = report_init("Card BLE", card_ble_init());
	if (ble_ready) { switch_test_set_poll_handler(card_ble_check_pairing_button); }
	struct card_ble_clock clock;
	card_ble_clock_snapshot(&clock);
	err = sd_logger_set_boot_id(clock.boot_id);
	if (err) { printk("ERROR: history boot identity: %d\n", err); }
	err = sd_logger_init();
	if (err) { printk("ERROR: SD worker init: %d\n", err); }
	if (ble_ready) { (void)report_init("Card history", card_history_init()); }
	if (display_ready) {
		err = card_image_transfer_init();
		if (err) { printk("ERROR: image service init: %d\n", err); }
	}

	printk("CARD CADENCE: sample=1 s BLE=60 s SD=%u s display=%u s%s\n",
	       CARD_IO_PERIOD_MS / 1000, CARD_IO_PERIOD_MS / 1000,
	       IS_ENABLED(CONFIG_CARD_TEST_CADENCE) ? " (TEST)" : "");
	printk("CARD POWER: System ON; no System OFF; SGP41/STCC4/OPT continuous; switches=50ms\n");
	if (alarm_ready && IS_ENABLED(CONFIG_CARD_ALARM_TEST_ON_BOOT)) {
		printk("CO2 ALERT TEST: one-shot alarm only; actual sensor values unchanged\n");
		co2_alarm_evaluate(true, 1201);
	}
	k_sem_give(&acquisition_start);
	k_sem_take(&first_sample_available, K_FOREVER);
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	int64_t origin_ms = latest_snapshot.started_ms;
	k_mutex_unlock(&snapshot_lock);
	int64_t next_ble = origin_ms + CARD_BLE_PERIOD_MS;
	int64_t next_io = origin_ms + CARD_IO_PERIOD_MS;
	if (ble_ready) { publish_snapshot(); }
	request_display();

	for (;;) {
		k_sleep(K_TIMEOUT_ABS_MS(MIN(next_ble, next_io)));
		int64_t now = k_uptime_get();
		if (now >= next_ble) {
			if (ble_ready) { publish_snapshot(); }
			next_ble = card_cadence_advance(next_ble, k_uptime_get(), CARD_BLE_PERIOD_MS);
			/* Slow driver recovery cannot hold up the dedicated 1-Hz thread. */
			recover_sensors();
		}
		now = k_uptime_get();
		if (now >= next_io) {
			err = sd_logger_flush_async();
			if (err && err != -EALREADY) { printk("ERROR: SD flush request: %d\n", err); }
			request_display();
			next_io = card_cadence_advance(next_io, k_uptime_get(), CARD_IO_PERIOD_MS);
		}
	}
}
