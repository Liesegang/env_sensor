#ifndef CARD_CADENCE_H_
#define CARD_CADENCE_H_

#include <stdint.h>

#define CARD_SAMPLE_PERIOD_MS 1000
#define CARD_BLE_PERIOD_MS 60000
#if defined(CONFIG_CARD_TEST_CADENCE) && CONFIG_CARD_TEST_CADENCE
#define CARD_IO_PERIOD_MS 40000
#else
#define CARD_IO_PERIOD_MS 600000
#endif

/* Preserve phase after slow I/O. Never issue a burst of stale periods. */
static inline int64_t card_cadence_advance(int64_t deadline, int64_t now, int64_t period)
{
	if (period <= 0 || now < deadline) { return deadline; }
	return deadline + ((now - deadline) / period + 1) * period;
}

#endif
