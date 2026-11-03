#ifndef CO2_ALARM_MOCK_H_
#define CO2_ALARM_MOCK_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define BUILD_ASSERT(value, message) _Static_assert(value, message)
#define ARG_UNUSED(value) (void)(value)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define K_FOREVER (-1)
#define K_NO_WAIT 0
#define K_MSEC(value) (value)
#define PWM_POLARITY_NORMAL 0
#define GPIO_OUTPUT_INACTIVE 0x10
#define DT_ALIAS(value) 0
#define DT_PATH(value) 0
#define DT_PWMS_FLAGS(node) PWM_POLARITY_NORMAL
#define DT_GPIO_PIN(node, property) 1

struct device { int port; };
static const struct device mock_pwm_device = { .port = -1 };
static const struct device mock_gpio_device = { .port = 1 };
struct pwm_dt_spec {
	const struct device *dev;
	uint32_t channel;
	uint32_t period;
	uint32_t flags;
};
struct gpio_dt_spec {
	const struct device *port;
	uint32_t pin;
	uint32_t dt_flags;
};
#define PWM_DT_SPEC_GET(node) \
	{ &mock_pwm_device, 0, 1000000000U / 2700U, PWM_POLARITY_NORMAL }
#define GPIO_DT_SPEC_GET(node, property) { &mock_gpio_device, 1, 0 }

struct k_mutex { bool locked; };
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable {
	struct k_work work;
	bool pending;
	int64_t due;
};
#define K_MUTEX_DEFINE(name) struct k_mutex name
#define K_WORK_DELAYABLE_DEFINE(name, callback) \
	struct k_work_delayable name = { .work = { .handler = callback } }

static int64_t mock_now;
static bool mock_in_worker;
static bool mock_locked;
static bool mock_pwm_ready = true;
static bool mock_gpio_ready = true;
static bool mock_runtime_enabled;
static bool mock_runtime_active;
static bool mock_pin_high;
static int mock_enable_error, mock_get_error, mock_put_error;
static int mock_pwm_start_error, mock_pwm_stop_error, mock_gpio_error;
static int mock_enable_calls, mock_get_calls, mock_put_calls;
static int mock_pwm_start_calls, mock_pwm_stop_calls, mock_gpio_calls;
static int mock_schedule_calls, mock_cancel_calls;
static int mock_schedule_failure_call;
static int mock_schedule_error;
static int mock_schedule_success = 1;
static int64_t mock_start_times[32], mock_stop_times[32];
static size_t mock_start_count, mock_stop_count;
static struct k_work_delayable *mock_scheduled_work;
static char mock_log[16384];
static size_t mock_log_size;

static int mock_printk(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int written = vsnprintf(mock_log + mock_log_size,
				sizeof(mock_log) - mock_log_size, format, args);
	va_end(args);
	assert(written >= 0);
	assert((size_t)written < sizeof(mock_log) - mock_log_size);
	mock_log_size += (size_t)written;
	return written;
}
#define printk mock_printk

static int k_mutex_lock(struct k_mutex *mutex, int timeout)
{
	assert(timeout == K_FOREVER && !mutex->locked && !mock_locked);
	mutex->locked = true;
	mock_locked = true;
	return 0;
}
static int k_mutex_unlock(struct k_mutex *mutex)
{
	assert(mutex->locked && mock_locked);
	mutex->locked = false;
	mock_locked = false;
	return 0;
}
static int64_t k_uptime_get(void) { return mock_now; }
static int k_work_reschedule(struct k_work_delayable *work, int64_t delay)
{
	assert(mock_locked && delay >= 0);
	mock_scheduled_work = work;
	++mock_schedule_calls;
	if (mock_schedule_calls == mock_schedule_failure_call) {
		return mock_schedule_error;
	}
	work->due = mock_now + delay;
	work->pending = true;
	return mock_schedule_success;
}
static int k_work_cancel_delayable(struct k_work_delayable *work)
{
	assert(mock_locked);
	++mock_cancel_calls;
	work->pending = false;
	return 0;
}
static bool pwm_is_ready_dt(const struct pwm_dt_spec *spec)
{
	assert(mock_locked && spec->dev == &mock_pwm_device);
	return mock_pwm_ready;
}
static bool gpio_is_ready_dt(const struct gpio_dt_spec *spec)
{
	assert(mock_locked && spec->port == &mock_gpio_device && spec->pin == 1);
	return mock_gpio_ready;
}
static int pm_device_runtime_enable(const struct device *device)
{
	assert(mock_locked && device == &mock_pwm_device);
	++mock_enable_calls;
	if (!mock_enable_error) { mock_runtime_enabled = true; }
	return mock_enable_error;
}
static int pm_device_runtime_get(const struct device *device)
{
	assert(mock_locked && mock_in_worker && device == &mock_pwm_device);
	assert(mock_runtime_enabled && !mock_runtime_active);
	++mock_get_calls;
	if (!mock_get_error) { mock_runtime_active = true; }
	return mock_get_error;
}
static int pm_device_runtime_put(const struct device *device)
{
	assert(mock_locked && device == &mock_pwm_device && mock_runtime_active);
	++mock_put_calls;
	if (!mock_put_error) { mock_runtime_active = false; }
	return mock_put_error;
}
static int pwm_set_dt(const struct pwm_dt_spec *spec, uint32_t period,
		      uint32_t pulse)
{
	assert(mock_locked && spec->dev == &mock_pwm_device && mock_runtime_active);
	assert(period == 1000000000U / 2700U && spec->flags == PWM_POLARITY_NORMAL);
	if (pulse) {
		assert(mock_in_worker && pulse == period / 2);
		++mock_pwm_start_calls;
		if (mock_pwm_start_error) { return mock_pwm_start_error; }
		assert(!mock_pin_high && mock_start_count < 32);
		mock_start_times[mock_start_count++] = mock_now;
		mock_pin_high = true;
	} else {
		++mock_pwm_stop_calls;
		if (mock_pwm_stop_error) { return mock_pwm_stop_error; }
		if (mock_pin_high) {
			assert(mock_stop_count < 32);
			mock_stop_times[mock_stop_count++] = mock_now;
		}
		mock_pin_high = false;
	}
	return 0;
}
static int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, int flags)
{
	assert(mock_locked && spec->port->port == 1 && spec->pin == 1);
	assert(flags == GPIO_OUTPUT_INACTIVE);
	++mock_gpio_calls;
	if (!mock_gpio_error) { mock_pin_high = false; }
	return mock_gpio_error;
}

static bool mock_work_pending(void)
{
	return mock_scheduled_work && mock_scheduled_work->pending;
}
static void mock_run_next(void)
{
	assert(mock_work_pending() && !mock_locked && !mock_in_worker);
	if (mock_now < mock_scheduled_work->due) { mock_now = mock_scheduled_work->due; }
	mock_scheduled_work->pending = false;
	mock_in_worker = true;
	mock_scheduled_work->work.handler(&mock_scheduled_work->work);
	mock_in_worker = false;
	assert(!mock_locked);
}
static void mock_run_all(void)
{
	int callbacks = 0;
	while (mock_work_pending()) {
		assert(++callbacks <= 20);
		mock_run_next();
	}
}
static void mock_run_stale_callback(void)
{
	assert(mock_scheduled_work && !mock_locked && !mock_work_pending());
	mock_in_worker = true;
	mock_scheduled_work->work.handler(&mock_scheduled_work->work);
	mock_in_worker = false;
	assert(!mock_locked);
}

#endif
