#ifndef OUTDOOR_CLOCK_H
#define OUTDOOR_CLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
int outdoor_clock_set_packet(const void *data, size_t size);
bool outdoor_clock_at(int64_t uptime_ms, int64_t *unix_ms);
int outdoor_clock_iso(int64_t unix_ms, char *output, size_t capacity);
#endif
