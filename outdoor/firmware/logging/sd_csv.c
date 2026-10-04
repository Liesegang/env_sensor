#include <sd_csv.h>
#include <outdoor_clock.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
struct csv_field { uint8_t sensor, offset, width, is_signed, hex; uint16_t divisor; };
#include <sd_csv_schema.h>
struct writer { char *output; size_t size, capacity; bool failed; };
static void append(struct writer *w, const char *format, ...)
{
	if (w->failed) { return; }
	va_list args;
	va_start(args, format);
	int n = vsnprintf(w->output + w->size, w->capacity - w->size, format, args);
	va_end(args);
	if (n < 0 || (size_t)n >= w->capacity - w->size) { w->failed = true; return; }
	w->size += n;
}
static uint64_t number(const uint8_t *p, unsigned int width)
{
	uint64_t n = 0;
	for (unsigned int i = 0; i < width; i++) { n |= (uint64_t)p[i] << (8 * i); }
	return n;
}
const char *sd_csv_header(void) { return SD_CSV_HEADER; }
int sd_csv_row(const uint8_t *packet, size_t size, int64_t unix_ms, char *output, size_t capacity)
{
	if (!packet || !output || !capacity || size < 24 || size > SD_LOG_PACKET_MAX ||
	    packet[0] != 'O' || packet[1] != 'E' || packet[2] != 1 || packet[3] != 24 || number(packet + 4, 2) != size) { return -EINVAL; }
	const uint8_t *blocks[8] = {0};
	unsigned int enabled = number(packet + 6, 2), valid = number(packet + 8, 2);
	if (enabled & ~0x7f || valid & ~enabled) { return -EINVAL; }
	for (size_t offset = 24; offset < size;) {
		if (offset + 6 > size) { return -EINVAL; }
		unsigned int id = packet[offset], length = number(packet + offset + 2, 2);
		if (!id || id > 7 || blocks[id] || offset + 4 + length > size || length < 2) { return -EINVAL; }
		const uint8_t *block = packet + offset + 4;
		int16_t error = (int16_t)number(block, 2);
		if (error > 0 || !(enabled & (1 << (id - 1))) || !!(valid & (1 << (id - 1))) != (error == 0) ||
		    length != (error == 0 ? csv_sensor_sizes[id] : 2)) { return -EINVAL; }
		blocks[id] = block;
		offset += 4 + length;
	}
	for (unsigned int id = 1; id <= 7; id++) {
		if (!!blocks[id] != !!(enabled & (1 << (id - 1)))) { return -EINVAL; }
	}
	char timestamp[25] = "";
	if (unix_ms >= 0 && outdoor_clock_iso(unix_ms, timestamp, sizeof(timestamp)) < 0) { return -ERANGE; }
	struct writer w = {output, 0, capacity, false};
	append(&w, "%s,%u,%llu,%llu,%u,%u", timestamp, unix_ms >= 0,
		(unsigned long long)number(packet + 12, 4), (unsigned long long)number(packet + 16, 8), enabled, valid);
	unsigned int previous = 0;
	for (size_t i = 0; i < sizeof(csv_fields) / sizeof(csv_fields[0]); i++) {
		const struct csv_field *f = &csv_fields[i];
		const uint8_t *block = blocks[f->sensor];
		if (previous != f->sensor) {
			append(&w, ",");
			if (block) { append(&w, "%d", (int16_t)number(block, 2)); }
			previous = f->sensor;
		}
		append(&w, ",");
		if (!block || number(block, 2)) { continue; }
		uint64_t raw = number(block + f->offset, f->width);
		if (f->hex) { append(&w, "0x%llx", (unsigned long long)raw); continue; }
		int64_t value = f->is_signed && (raw & (1ULL << (f->width * 8 - 1))) ? (int64_t)(raw | (~0ULL << (f->width * 8))) : (int64_t)raw;
		if (f->divisor == 1) {
			if (f->is_signed) { append(&w, "%lld", (long long)value); }
			else { append(&w, "%llu", (unsigned long long)raw); }
		}
		else {
			uint64_t magnitude = value < 0 ? (uint64_t)-value : (uint64_t)value;
			append(&w, "%s%llu.%0*llu", value < 0 ? "-" : "", (unsigned long long)(magnitude / f->divisor),
				f->divisor == 100 ? 2 : 3, (unsigned long long)(magnitude % f->divisor));
		}
	}
	append(&w, "\r\n");
	return w.failed ? -ENOSPC : (int)w.size;
}
