#ifndef OUTDOOR_SD_LOG_H
#define OUTDOOR_SD_LOG_H
#include <telemetry.h>
/* Nonblocking producer; all filesystem work runs on the logger thread. */
void sd_log_submit(const struct telemetry_sample *sample);
#endif
