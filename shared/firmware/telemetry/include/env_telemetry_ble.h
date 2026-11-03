/* SPDX-License-Identifier: MIT */
#ifndef ENV_TELEMETRY_BLE_H_
#define ENV_TELEMETRY_BLE_H_
#include <env_telemetry.h>
#ifdef __cplusplus
extern "C" {
#endif

enum env_telemetry_pairing_event {
    ENV_TELEMETRY_PAIRING_OPENED,
    ENV_TELEMETRY_PAIRING_PASSKEY,
    ENV_TELEMETRY_PAIRING_CLOSED,
};

struct env_telemetry_ble_config {
    /* HCI units of 0.625 ms; zero selects 1600 (one second). */
    uint16_t advertising_interval_min;
    uint16_t advertising_interval_max;
    /* Optional OT v1 (12-byte UTC clock) receiver. NULL rejects clock writes. */
    int (*set_clock_packet)(const void *packet, size_t length, void *context);
    void *context;
    /* Enable per-publication cache/notification logs for bring-up. */
    bool log_publications;
    /* Zero: continuous peripheral. Nonzero: each publish opens this RF window;
     * advertising stops and the peer disconnects when the deadline expires.
     */
    uint32_t active_window_ms;
    /* Disable the BT/controller/MPSL
     * between windows, and re-enable from the dedicated sender thread.
     * Requires CONFIG_BT_UNINIT_MPSL_ON_DISABLE and dynamic interrupts.
     */
    bool disable_between_windows;
    /* CONFIG_ENV_TELEMETRY_SECURE: initial/physical enrollment duration.
     * Existing bonds continue to work outside this window. One owner only.
     */
    uint32_t pairing_window_ms;
    /* Must return promptly. PASSKEY prepares a fresh cryptographic random PIN
     * before advertising; render asynchronously and acknowledge completion.
     * Each request_id is unique in this boot and valid for one SMP attempt.
     */
    void (*pairing_event)(enum env_telemetry_pairing_event event,
                          unsigned int passkey, uint32_t request_id, void *context);
};

/* One peripheral connection; initialize once after caching an initial frame. */
int env_telemetry_ble_init(const struct env_telemetry_ble_config *config);
/* Publish only when a new application sample exists. No periodic RF sampling. */
int env_telemetry_ble_publish(const struct env_telemetry_sample *sample);
/* Accept an encoded OE v1 frame; copied synchronously, caller retains ownership. */
int env_telemetry_ble_publish_frame(const uint8_t *frame, size_t length);
/* Physical authorization only: disconnect, erase all peer bonds, open a new
 * enrollment window. Serialized on the sender thread; never from BLE Write.
 * Keeps the local identity. Returns -ENOTSUP for nonsecure configurations.
 */
int env_telemetry_ble_repair(void);
/* After the complete physical display refresh, authorize advertising this
 * prepared PIN. Stale completions are ignored; error fails enrollment closed.
 */
void env_telemetry_ble_pairing_display_ready(uint32_t request_id, int error);
struct bt_conn;
/* Authenticated auxiliary operation; renew its independent connection lease. */
int env_telemetry_ble_hold_connection(struct bt_conn *conn, uint32_t duration_ms);
/* Release only the auxiliary lease, preserving any notification drain lease. */
void env_telemetry_ble_release_connection(struct bt_conn *conn);
/* Independent history lease. Releasing image/history cannot shorten the other
 * operation's lease or the notification/ATT response drain grace.
 */
int env_telemetry_ble_hold_history(struct bt_conn *conn, uint32_t duration_ms);
/* Set this operation's exact bounded deadline (<=300000 ms), so switching
 * from preparation to data progress can shorten a longer history lease.
 * Image/notification/enrollment leases are preserved independently.
 */
int env_telemetry_ble_set_history_lease(struct bt_conn *conn, uint32_t duration_ms);
void env_telemetry_ble_release_history(struct bt_conn *conn);
#ifdef __cplusplus
}
#endif
#endif /* ENV_TELEMETRY_BLE_H_ */
