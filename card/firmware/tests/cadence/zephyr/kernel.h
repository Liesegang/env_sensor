#ifndef CARD_CADENCE_TEST_KERNEL_H_
#define CARD_CADENCE_TEST_KERNEL_H_
#include <stdint.h>

#define CONFIG_ENV_TELEMETRY_SECURE 1
#define IS_ENABLED(value) (value)
#define ARG_UNUSED(value) (void)(value)
#define K_FOREVER -1

struct k_mutex { int locked; };
#define K_MUTEX_DEFINE(name) struct k_mutex name
static inline int k_mutex_lock(struct k_mutex *mutex, int timeout)
{
	(void)timeout;
	mutex->locked = 1;
	return 0;
}
static inline int k_mutex_unlock(struct k_mutex *mutex)
{
	mutex->locked = 0;
	return 0;
}

static int64_t mock_now;
static unsigned int mock_sleep_calls;
static inline int64_t k_uptime_get(void) { return mock_now; }
static inline void k_msleep(int milliseconds)
{
	++mock_sleep_calls;
	mock_now += milliseconds;
}
#endif
