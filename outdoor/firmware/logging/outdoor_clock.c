#include <outdoor_clock.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <zephyr/kernel.h>

static struct k_spinlock clock_lock;
static bool synchronized;
static int64_t epoch_offset;

int outdoor_clock_set_packet(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	if (!bytes || size != 12 || bytes[0] != 'O' || bytes[1] != 'T' || bytes[2] != 1 || bytes[3] != 0) {
		return -EINVAL;
	}
	uint64_t epoch = 0;
	for (unsigned int i = 0; i < 8; i++) { epoch |= (uint64_t)bytes[4 + i] << (8 * i); }
	/* 2020-01-01 through 2100-01-01, UTC, milliseconds. */
	if (epoch < 1577836800000ULL || epoch >= 4102444800000ULL) { return -ERANGE; }
	k_spinlock_key_t key = k_spin_lock(&clock_lock);
	epoch_offset = (int64_t)epoch - k_uptime_get();
	synchronized = true;
	k_spin_unlock(&clock_lock, key);
	return 0;
}

bool outdoor_clock_at(int64_t uptime_ms, int64_t *unix_ms)
{
	if (!unix_ms) { return false; }
	k_spinlock_key_t key = k_spin_lock(&clock_lock);
	bool valid = synchronized;
	if (valid) { *unix_ms = epoch_offset + uptime_ms; }
	k_spin_unlock(&clock_lock, key);
	return valid;
}

int outdoor_clock_iso(int64_t unix_ms, char *output, size_t capacity)
{
	if (unix_ms < 0 || !output || capacity < 25) { return -EINVAL; }
	time_t seconds = unix_ms / 1000;
	struct tm utc;
	if (!gmtime_r(&seconds, &utc)) { return -ERANGE; }
	int length = snprintf(output, capacity, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
		utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
		(int)(unix_ms % 1000));
	return length < 0 || (size_t)length >= capacity ? -ENOSPC : length;
}
