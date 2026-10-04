#ifndef OUTDOOR_SD_CSV_H
#define OUTDOOR_SD_CSV_H
#include <stddef.h>
#include <stdint.h>
#define SD_LOG_PACKET_MAX 258
const char *sd_csv_header(void);
int sd_csv_row(const uint8_t *packet, size_t size, int64_t unix_ms, char *output, size_t capacity);
#endif
