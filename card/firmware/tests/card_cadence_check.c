#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/card_cadence.h"

static void check_deadlines(void)
{
	assert(CARD_SAMPLE_PERIOD_MS == 1000);
	assert(CARD_BLE_PERIOD_MS == 60000);
#if defined(CONFIG_CARD_TEST_CADENCE) && CONFIG_CARD_TEST_CADENCE
	assert(CARD_IO_PERIOD_MS == 40000);
#else
	assert(CARD_IO_PERIOD_MS == 600000);
#endif
	/* The event is due at its deadline; advancing selects the next future
	 * deadline, retains the original phase, and never replays missed events. */
	assert(card_cadence_advance(1000, 999, 1000) == 1000);
	assert(card_cadence_advance(1000, 1000, 1000) == 2000);
	assert(card_cadence_advance(1000, 1999, 1000) == 2000);
	assert(card_cadence_advance(1000, 2000, 1000) == 3000);
	assert(card_cadence_advance(137, 4999, 1000) == 5137);
	assert(card_cadence_advance(1000, 900123, 1000) == 901000);
	assert(card_cadence_advance(1000, 2000, 0) == 1000);
	assert(card_cadence_advance(1000, 2000, -1) == 1000);
	/* Uptime/deadlines must remain correct after signed 32-bit milliseconds. */
	int64_t deadline = INT64_C(4294967137);
	assert(card_cadence_advance(deadline, deadline + 12345, 1000) == deadline + 13000);
}

static void check_ten_minutes(void)
{
	int64_t next_sample = CARD_SAMPLE_PERIOD_MS;
	int64_t next_ble = CARD_BLE_PERIOD_MS;
	int64_t next_io = CARD_IO_PERIOD_MS;
	unsigned int samples = 0, ble = 0, sd = 0, display = 0;
	/* Exclude the separate startup sample: count seconds 1 through 600.
	 * The sample clock and the application I/O clock have separate deadlines. */
	for (int64_t now = 0; now <= 600000; now += 50) {
		if (now >= next_sample) {
			++samples;
			next_sample = card_cadence_advance(next_sample, now, CARD_SAMPLE_PERIOD_MS);
		}
		if (now >= next_ble) {
			assert(samples == (unsigned int)(now / 1000));
			++ble;
			next_ble = card_cadence_advance(next_ble, now, CARD_BLE_PERIOD_MS);
		}
		if (now >= next_io) {
			++sd;
			++display;
			next_io = card_cadence_advance(next_io, now, CARD_IO_PERIOD_MS);
		}
	}
	assert(samples == 600 && ble == 10);
	assert(sd == 600000 / CARD_IO_PERIOD_MS && display == sd);
	assert(next_sample == 601000 && next_ble == 660000);
	assert(next_io == 600000 + CARD_IO_PERIOD_MS);
}

static void check_slow_io(void)
{
	int64_t next_sample = CARD_SAMPLE_PERIOD_MS;
	int64_t next_ble = CARD_BLE_PERIOD_MS;
	int64_t next_io = CARD_IO_PERIOD_MS;
	int64_t io_started = -1;
	int64_t io_finished = -1;
	unsigned int samples = 0, ble = 0, io = 0, samples_while_io = 0;
	int64_t stop = CARD_IO_PERIOD_MS + 45000;
	/* A 37.5-second SD operation blocks its caller. The independent sampler
	 * keeps running, while the next I/O deadline advances after completion. */
	for (int64_t now = 0; now <= stop; now += 50) {
		if (now >= next_sample) {
			++samples;
			if (io_started >= 0 && now < io_finished) { ++samples_while_io; }
			next_sample = card_cadence_advance(next_sample, now, CARD_SAMPLE_PERIOD_MS);
		}
		if (io_started >= 0 && now < io_finished) { continue; }
		if (io_started >= 0) {
			next_io = card_cadence_advance(next_io, now, CARD_IO_PERIOD_MS);
			io_started = -1;
		}
		if (now >= next_ble) {
			++ble;
			next_ble = card_cadence_advance(next_ble, now, CARD_BLE_PERIOD_MS);
			assert(next_ble > now); /* One publication, no catch-up burst. */
		}
		if (now >= next_io) {
			++io;
			io_started = now;
			io_finished = now + 37500;
		}
	}
	assert(samples == (unsigned int)(stop / 1000));
	assert(samples_while_io >= 37);
	assert(ble == (unsigned int)(stop / CARD_BLE_PERIOD_MS));
	assert(io == (unsigned int)(stop / CARD_IO_PERIOD_MS));
	assert(next_sample == stop + CARD_SAMPLE_PERIOD_MS);
}

static void check_late_sample(void)
{
	int64_t next = 1000;
	unsigned int reads = 0;
	/* A late sensor read can skip missed periods, but must not run a burst
	 * of meaningless historical reads at the same uptime. */
	const int64_t wakes[] = {999, 1000, 4750, 4750, 4999, 5000};
	for (size_t i = 0; i < sizeof(wakes) / sizeof(wakes[0]); ++i) {
		if (wakes[i] >= next) {
			++reads;
			next = card_cadence_advance(next, wakes[i], CARD_SAMPLE_PERIOD_MS);
			assert(next > wakes[i]);
		}
	}
	assert(reads == 3 && next == 6000);
}

int main(void)
{
	check_deadlines();
	check_ten_minutes();
	check_slow_io();
	check_late_sample();
	printf("Card cadence PASS: sample=%d ms BLE=%d ms SD/display=%d ms\n",
	       CARD_SAMPLE_PERIOD_MS, CARD_BLE_PERIOD_MS, CARD_IO_PERIOD_MS);
}
