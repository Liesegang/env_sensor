#include <errno.h>
#include "alarms/co2_alarm_mock.h"
/* Compile the real driver; the include path replaces only Zephyr/hardware APIs. */
#include "../src/co2_alarm.c"

static void assert_silent(void)
{
	assert(!mock_pin_high && !mock_runtime_active && !mock_work_pending());
	assert(!mock_locked && !mock_in_worker);
}
static void init_success(void)
{
	assert(co2_alarm_init() == 0);
	assert(mock_enable_calls == 1 && mock_gpio_calls == 1);
	assert(mock_start_count == 0 && mock_get_calls == 0);
	assert_silent();
	assert(co2_alarm_init() == 0);
	assert(mock_enable_calls == 1 && mock_gpio_calls == 1);
}
static void start_async(void)
{
	int64_t before = mock_now;
	int gets_before = mock_get_calls;
	size_t starts_before = mock_start_count;
	co2_alarm_evaluate(true, 1201);
	assert(mock_work_pending() && mock_scheduled_work->due == before);
	assert(mock_now == before && mock_get_calls == gets_before);
	assert(mock_start_count == starts_before && !mock_pin_high);
}
static void assert_fault_does_not_restart(void)
{
	int schedules_before = mock_schedule_calls;
	int starts_before = mock_pwm_start_calls;
	co2_alarm_evaluate(true, 0);
	co2_alarm_evaluate(true, 1201);
	assert(mock_schedule_calls == schedules_before);
	assert(mock_pwm_start_calls == starts_before);
	assert_silent();
}
static void test_normal(void)
{
	/* Nothing can run before successful initialization. */
	co2_alarm_evaluate(true, 5000);
	assert(mock_schedule_calls == 0 && mock_enable_calls == 0);
	init_success();
	co2_alarm_evaluate(false, 5000);
	co2_alarm_evaluate(true, -1);
	co2_alarm_evaluate(true, 0);
	co2_alarm_evaluate(true, 1199);
	co2_alarm_evaluate(true, 1200);
	assert(mock_schedule_calls == 0 && mock_get_calls == 0);
	mock_now = 123;
	start_async();
	mock_schedule_success = 0; /* Zero and positive reschedule results are success. */
	mock_run_all();
	assert(mock_start_count == 5 && mock_stop_count == 5);
	for (size_t i = 0; i < 5; ++i) {
		assert(mock_start_times[i] == 123 + (int64_t)i * 1000);
		assert(mock_stop_times[i] == mock_start_times[i] + 100);
	}
	assert(mock_get_calls == 5 && mock_put_calls == 5);
	assert(mock_pwm_start_calls == 5 && mock_pwm_stop_calls == 5);
	assert_silent();
	int schedules_before = mock_schedule_calls;
	co2_alarm_evaluate(true, 1201);
	co2_alarm_evaluate(true, INT32_MAX);
	co2_alarm_evaluate(false, 0);
	co2_alarm_evaluate(false, 1200);
	co2_alarm_evaluate(true, -1);
	co2_alarm_evaluate(true, INT32_MIN);
	co2_alarm_evaluate(true, 1201);
	assert(mock_schedule_calls == schedules_before && mock_start_count == 5);
	co2_alarm_evaluate(true, 1200);
	start_async();
	mock_run_all();
	assert(mock_start_count == 10 && mock_stop_count == 10);
	assert_silent();
}
static void test_late_worker(void)
{
	init_success();
	start_async();
	mock_run_next(); /* First tone at 0, stop at 100. */
	mock_run_next();
	mock_now = 1800; /* The second start was queued for 1000. */
	mock_run_next();
	mock_run_all();
	assert(mock_start_count == 5 && mock_stop_count == 5);
	assert(mock_start_times[0] == 0 && mock_start_times[1] == 1800);
	for (size_t i = 1; i < 5; ++i) {
		assert(mock_start_times[i] - mock_start_times[i - 1] >= 1000);
		assert(mock_stop_times[i] - mock_start_times[i] == 100);
	}
	assert_silent();
}
static void test_stop(bool before_start)
{
	init_success();
	start_async();
	if (!before_start) {
		mock_run_next();
		assert(mock_pin_high && mock_runtime_active);
		mock_now = 37;
	}
	assert(co2_alarm_stop() == 0);
	assert(mock_cancel_calls == 1);
	assert_silent();
	int gets_before = mock_get_calls;
	int schedules_before = mock_schedule_calls;
	mock_now = 10000;
	mock_run_stale_callback(); /* A previously dispatched callback must stay quiet. */
	co2_alarm_evaluate(true, 5000);
	assert(mock_get_calls == gets_before && mock_schedule_calls == schedules_before);
	assert_silent();
	assert(co2_alarm_stop() == 0);
	co2_alarm_evaluate(true, 1200);
	start_async();
	mock_run_all();
	assert(mock_start_count == (before_start ? 5U : 6U));
	assert_silent();
}
static void test_init_failure(const char *stage)
{
	int expected;
	if (!strcmp(stage, "init_pwm")) { mock_pwm_ready = false; expected = -ENODEV; }
	else if (!strcmp(stage, "init_gpio")) { mock_gpio_ready = false; expected = -ENODEV; }
	else if (!strcmp(stage, "init_runtime")) { mock_enable_error = -EIO; expected = -EIO; }
	else { mock_gpio_error = -EACCES; expected = -EACCES; }
	assert(co2_alarm_init() == expected);
	assert_fault_does_not_restart();
	assert(mock_get_calls == 0 && mock_pwm_start_calls == 0);
	mock_gpio_error = 0;
	assert(co2_alarm_stop() == 0);
	assert_silent();
}
static void test_start_failure(bool runtime)
{
	init_success();
	start_async();
	if (runtime) { mock_get_error = -EIO; }
	else { mock_pwm_start_error = -EINVAL; }
	mock_run_next();
	assert(mock_get_calls == 1 && mock_start_count == 0);
	assert(mock_put_calls == (runtime ? 0 : 1));
	assert(mock_cancel_calls == 1);
	assert(strstr(mock_log, runtime ? "PWM=-5 cleanup=0" : "PWM=-22 cleanup=0"));
	assert_fault_does_not_restart();
}
static void test_schedule_failure(int failing_call)
{
	init_success();
	mock_schedule_failure_call = failing_call;
	mock_schedule_error = -ENOMEM;
	co2_alarm_evaluate(true, 1201);
	while (mock_work_pending()) { mock_run_next(); }
	assert(mock_schedule_calls == failing_call && mock_cancel_calls == 1);
	assert(mock_start_count == (failing_call == 1 ? 0U : 1U));
	assert(strstr(mock_log, "PWM=-12 cleanup=0"));
	assert_fault_does_not_restart();
}
static void test_stop_errors(const char *stage)
{
	init_success();
	start_async();
	mock_run_next();
	int expected;
	if (!strcmp(stage, "stop_first_error")) {
		mock_pwm_stop_error = -EINVAL;
		mock_put_error = -EIO;
		mock_gpio_error = -EACCES;
		expected = -EINVAL;
	} else if (!strcmp(stage, "stop_runtime_error")) {
		mock_put_error = -EIO;
		mock_gpio_error = -EACCES;
		expected = -EIO;
	} else if (!strcmp(stage, "stop_pwm_error")) {
		mock_pwm_stop_error = -EINVAL;
		expected = -EINVAL;
	} else {
		mock_gpio_error = -EACCES;
		expected = -EACCES;
	}
	int gpio_before = mock_gpio_calls;
	assert(co2_alarm_stop() == expected);
	/* Cleanup attempts every operation, even when its predecessor failed. */
	assert(mock_pwm_stop_calls == 1 && mock_put_calls == 1);
	assert(mock_gpio_calls == gpio_before + 1 && !mock_work_pending());
	if (!mock_put_error) { assert(!mock_runtime_active); }
	if (!mock_gpio_error || !mock_pwm_stop_error) { assert(!mock_pin_high); }
	mock_pwm_stop_error = mock_put_error = mock_gpio_error = 0;
	assert(co2_alarm_stop() == 0);
	assert_silent();
}
static void test_worker_stop_failure(void)
{
	init_success();
	start_async();
	mock_run_next();
	mock_pwm_stop_error = -EIO;
	mock_run_next();
	assert(mock_start_count == 1 && mock_cancel_calls == 1);
	assert(mock_put_calls == 1 && mock_gpio_calls == 3);
	assert(strstr(mock_log, "PWM=-5 cleanup=0"));
	assert_fault_does_not_restart();
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	const char *test = argv[1];
	if (!strcmp(test, "normal")) { test_normal(); }
	else if (!strcmp(test, "late_worker")) { test_late_worker(); }
	else if (!strcmp(test, "stop_pending")) { test_stop(true); }
	else if (!strcmp(test, "stop_active")) { test_stop(false); }
	else if (!strncmp(test, "init_", 5)) { test_init_failure(test); }
	else if (!strcmp(test, "runtime_get")) { test_start_failure(true); }
	else if (!strcmp(test, "pwm_start")) { test_start_failure(false); }
	else if (!strcmp(test, "schedule_initial")) { test_schedule_failure(1); }
	else if (!strcmp(test, "schedule_stop")) { test_schedule_failure(2); }
	else if (!strcmp(test, "schedule_next")) { test_schedule_failure(3); }
	else if (!strcmp(test, "worker_stop")) { test_worker_stop_failure(); }
	else if (!strncmp(test, "stop_", 5)) { test_stop_errors(test); }
	else { assert(!"Unknown test case"); }
	printf("PASS co2_alarm %s\n", test);
	return 0;
}
