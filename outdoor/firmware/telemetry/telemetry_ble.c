/* SPDX-License-Identifier: MIT */
/* Keep the Outdoor API while using the common environment sensor transport. */
#include <telemetry.h>
#include <env_telemetry_ble.h>
#include <outdoor_clock.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static int synchronize_clock(const void *packet, size_t length, void *context)
{
    ARG_UNUSED(context);
    return outdoor_clock_set_packet(packet, length);
}

int telemetry_ble_init(void)
{
    struct telemetry_sample initial = {
        .uptime_ms = k_uptime_get(), .enabled_mask = 0x0f,
        .opt4001.error = -EAGAIN, .bme690.error = -EAGAIN,
        .sgp41.error = -EAGAIN, .stcc4.error = -EAGAIN,
        .bmv080.error = -EAGAIN, .as3935.error = -EAGAIN, .sfa40.error = -EAGAIN,
    };
#if defined(CONFIG_APP_BMV080)
    initial.enabled_mask |= 0x10;
#endif
#if defined(CONFIG_APP_AS3935)
    initial.enabled_mask |= 0x20;
#endif
#if defined(CONFIG_APP_SFA40)
    initial.enabled_mask |= 0x40;
#endif
    telemetry_ble_publish(&initial);
    const struct env_telemetry_ble_config config = {
        .set_clock_packet = synchronize_clock,
        /* Keep Outdoor's existing advertising timing. Card uses 1s defaults. */
        .advertising_interval_min = 160, .advertising_interval_max = 240,
    };
    return env_telemetry_ble_init(&config);
}

void telemetry_ble_publish(const struct telemetry_sample *sample)
{
    uint8_t frame[TELEMETRY_MAX_SIZE];
    size_t length = telemetry_encode(sample, frame, sizeof(frame));
    int err = length ? env_telemetry_ble_publish_frame(frame, length) : -EINVAL;
    if (err) { printk("ERROR: BLE packet encoding/publishing failed: %d\n", err); }
}
