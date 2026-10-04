#include <buzzer.h>

#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static const struct pwm_dt_spec buzzer_pwm = PWM_DT_SPEC_GET(DT_ALIAS(buzzer));
static bool initialized;

#define BUZZER_PERIOD_NS (NSEC_PER_SEC / CONFIG_BUZZER_FREQUENCY_HZ)
#define BUZZER_PULSE_NS (BUZZER_PERIOD_NS * CONFIG_BUZZER_DUTY_PERMILLE / 1000U)

BUILD_ASSERT(CONFIG_BUZZER_DUTY_PERMILLE <= 500, "Buzzer duty must be <= 50%");
BUILD_ASSERT(BUZZER_PULSE_NS > 0, "Buzzer pulse must be nonzero");
BUILD_ASSERT(DT_PWMS_FLAGS(DT_ALIAS(buzzer)) == PWM_POLARITY_NORMAL,
	     "TR1 needs active-high drive");

int buzzer_init(void)
{
	initialized = false;
	if (!pwm_is_ready_dt(&buzzer_pwm)) {
		return -ENODEV;
	}

	int err = pwm_set_dt(&buzzer_pwm, BUZZER_PERIOD_NS, 0);

	if (err != 0) {
		return err;
	}
	initialized = true;

	printk("Outdoor buzzer: %u Hz, duty %u/1000, period %u ns, pulse %u ns\n",
	       CONFIG_BUZZER_FREQUENCY_HZ, CONFIG_BUZZER_DUTY_PERMILLE,
	       (unsigned int)BUZZER_PERIOD_NS, (unsigned int)BUZZER_PULSE_NS);
	return 0;
}

int buzzer_start(void)
{
	if (!initialized) {
		return -EACCES;
	}

	int err = pwm_set_dt(&buzzer_pwm, BUZZER_PERIOD_NS, BUZZER_PULSE_NS);

	if (err != 0) {
		(void)buzzer_stop();
	}
	return err;
}

int buzzer_stop(void)
{
	if (!initialized) {
		return -EACCES;
	}

	return pwm_set_dt(&buzzer_pwm, BUZZER_PERIOD_NS, 0);
}
