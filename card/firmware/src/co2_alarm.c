#include "co2_alarm.h"

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static const struct pwm_dt_spec buzzer_pwm = PWM_DT_SPEC_GET(DT_ALIAS(buzzer));
static const struct gpio_dt_spec buzzer_pin =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), buzzer_drive_gpios);

#define CO2_THRESHOLD_PPM 1200
#define BEEP_COUNT 5
#define BEEP_DURATION_MS 100
#define BEEP_INTERVAL_MS 1000

BUILD_ASSERT(DT_PWMS_FLAGS(DT_ALIAS(buzzer)) == PWM_POLARITY_NORMAL,
	     "TR1 requires active-high PWM");
BUILD_ASSERT(DT_GPIO_PIN(DT_PATH(zephyr_user), buzzer_drive_gpios) == 1,
	     "Card v2 buzzer is on P1.01");

K_MUTEX_DEFINE(alarm_lock);
static bool initialized;
static bool armed = true;
static bool active;
static bool tone_on;
static bool pwm_acquired;
static uint8_t beeps_started;
static int64_t next_start_ms;

static void alarm_step(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(alarm_work, alarm_step);

/* Called with alarm_lock held. All cleanup steps run even after an error. */
static int silence_locked(void)
{
	int error = 0;
	if (pwm_acquired) {
		error = pwm_set_dt(&buzzer_pwm, buzzer_pwm.period, 0);
		int put_error = pm_device_runtime_put(buzzer_pwm.dev);
		if (!error) { error = put_error; }
		if (!put_error) { pwm_acquired = false; }
	}
	/* The sleep pinctrl disconnects PWM and makes P1.01 an input. Hold
	 * the transistor off explicitly after the synchronous PWM suspend. */
	int pin_error = gpio_pin_configure_dt(&buzzer_pin, GPIO_OUTPUT_INACTIVE);
	if (!error) { error = pin_error; }
	if (tone_on) {
		printk("CO2 ALERT: PWM stop %u/%u at %lld ms\n", beeps_started,
		       BEEP_COUNT, (long long)k_uptime_get());
	}
	tone_on = false;
	return error;
}

static void abort_locked(int error)
{
	active = false;
	initialized = false;
	(void)k_work_cancel_delayable(&alarm_work);
	int stop_error = silence_locked();
	printk("ERROR: CO2 alarm PWM=%d cleanup=%d\n", error, stop_error);
}

static void alarm_step(struct k_work *work)
{
	ARG_UNUSED(work);
	k_mutex_lock(&alarm_lock, K_FOREVER);
	if (!active) { k_mutex_unlock(&alarm_lock); return; }
	int error;
	int64_t deadline;
	if (tone_on) {
		error = silence_locked();
		if (error) { abort_locked(error); k_mutex_unlock(&alarm_lock); return; }
		if (beeps_started == BEEP_COUNT) {
			active = false;
			printk("CO2 ALERT: complete; PWM suspended and pin low\n");
			k_mutex_unlock(&alarm_lock);
			return;
		}
		deadline = next_start_ms;
	} else {
		error = pm_device_runtime_get(buzzer_pwm.dev);
		if (!error) {
			pwm_acquired = true;
			error = pwm_set_dt(&buzzer_pwm, buzzer_pwm.period, buzzer_pwm.period / 2);
		}
		if (error) { abort_locked(error); k_mutex_unlock(&alarm_lock); return; }
		tone_on = true;
		++beeps_started;
		int64_t started_ms = k_uptime_get();
		next_start_ms = started_ms + BEEP_INTERVAL_MS;
		deadline = started_ms + BEEP_DURATION_MS;
		printk("CO2 ALERT: PWM start %u/%u at %lld ms\n", beeps_started,
		       BEEP_COUNT, (long long)started_ms);
	}
	/* Never busy-wait. A delayed system queue may postpone a beep, but
	 * does not compress the start interval or create a sixth beep. */
	error = k_work_reschedule(&alarm_work, K_MSEC(MAX(0, deadline - k_uptime_get())));
	if (error < 0) { abort_locked(error); }
	k_mutex_unlock(&alarm_lock);
}

int co2_alarm_init(void)
{
	k_mutex_lock(&alarm_lock, K_FOREVER);
	if (initialized) { k_mutex_unlock(&alarm_lock); return 0; }
	int error = -ENODEV;
	if (pwm_is_ready_dt(&buzzer_pwm) && gpio_is_ready_dt(&buzzer_pin)) {
		error = pm_device_runtime_enable(buzzer_pwm.dev);
		if (!error) { error = silence_locked(); }
	}
	if (!error) {
		initialized = true;
		printk("CO2 ALERT READY: >%u ppm; PWM %u Hz 50%%; %u x %u ms every %u ms\n",
		       CO2_THRESHOLD_PPM, (unsigned int)(1000000000U / buzzer_pwm.period),
		       BEEP_COUNT, BEEP_DURATION_MS, BEEP_INTERVAL_MS);
	}
	k_mutex_unlock(&alarm_lock);
	return error;
}

void co2_alarm_evaluate(bool valid, int32_t co2_ppm)
{
	k_mutex_lock(&alarm_lock, K_FOREVER);
	if (!initialized || !valid || co2_ppm < 0) { k_mutex_unlock(&alarm_lock); return; }
	if (co2_ppm <= CO2_THRESHOLD_PPM) { armed = true; }
	else if (armed && !active) {
		armed = false;
		active = true;
		beeps_started = 0;
		printk("CO2 ALERT: threshold crossed; ppm=%d\n", co2_ppm);
		int error = k_work_reschedule(&alarm_work, K_NO_WAIT);
		if (error < 0) { abort_locked(error); }
	}
	k_mutex_unlock(&alarm_lock);
}

int co2_alarm_stop(void)
{
	k_mutex_lock(&alarm_lock, K_FOREVER);
	active = false;
	(void)k_work_cancel_delayable(&alarm_work);
	int error = silence_locked();
	k_mutex_unlock(&alarm_lock);
	return error;
}
