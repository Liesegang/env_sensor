/* State/lifecycle tests using the real transport and deterministic SDK stubs.
 * These verify application policy/order; actual SMP/flash/central behavior
 * still needs the hardware pairing/reboot test described in the README.
 */
#define SECURE_CHECK 1
#include "mock.h"
#include "../env_telemetry_ble.c"

static int ui_pin_count, ui_closed_count;
static uint32_t displayed_pin, displayed_request;
static uint8_t frame[24] = {'O','E',1,24,24,0,0,0,0,0,0,0,1};

static void ui_event(enum env_telemetry_pairing_event event, unsigned int pin,
                     uint32_t request, void *context)
{
    (void)context;
    if (event == ENV_TELEMETRY_PAIRING_PASSKEY) {
        displayed_pin = pin; displayed_request = request; ++ui_pin_count;
        assert(!mock_advertising); /* PIN refresh starts with no RF admission. */
    } else if (event == ENV_TELEMETRY_PAIRING_CLOSED) { ++ui_closed_count; }
}
static void run_work(struct k_work_delayable *work)
{
    work->pending = false; work->handler(&work->work);
}
static void sender_step(void)
{
    update_stack_lifecycle(); verify_owner_bond(); prepare_enrollment(); start_advertising(); notify_latest();
}
static void finish_disconnect(void)
{
    connection_callbacks.disconnected(mock_peer, 22);
    connection_callbacks.recycled();
}

int main(void)
{
    _Static_assert(DATA_PERMISSIONS == (BT_GATT_PERM_READ_LESC | BT_GATT_PERM_WRITE_LESC |
                   BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
                   "Read/Write/CCC all need authenticated LE Secure Connections");
    const struct env_telemetry_ble_config conf = {
        .active_window_ms = 5000, .disable_between_windows = true,
        .advertising_interval_min = 160, .advertising_interval_max = 160,
        .pairing_window_ms = 180000, .pairing_event = ui_event,
    };
    assert(!env_telemetry_ble_publish_frame(frame, sizeof(frame)));
    assert(!env_telemetry_ble_init(&conf));
    assert(!stack_enabled && new_pin_pending && !mock_advertising);
    assert(auth_register_count == 1 && info_register_count == 1 && settings_load_count == 1);
    mock_now = 1000;
    assert(!env_telemetry_ble_publish_frame(frame, sizeof(frame)));
    sender_step();
    assert(stack_enabled && settings_load_count == 2 && !mock_advertising);
    assert(pin_display_pending && displayed_pin == 123456 && displayed_request == 1);
    struct bt_conn peer = {.subscribed = true, .security = 1};
    assert(pairing_accept(&peer, NULL) == BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
    env_telemetry_ble_pairing_display_ready(displayed_request + 1, 0);
    sender_step(); assert(!mock_advertising);
    /* A previous graph refresh can take 28 s: SMP starts only AFTER it. */
    mock_now += 28000;
    env_telemetry_ble_pairing_display_ready(displayed_request, 0);
    sender_step(); assert(mock_advertising && enrollment_until_ms == mock_now + 180000);
    connected(&peer, 0); notify_latest(); assert(!notify_count);
    assert(pairing_accept(&peer, NULL) == BT_SECURITY_ERR_SUCCESS);
    assert(app_passkey(&peer) == displayed_pin && !pin_available);
    passkey_display(&peer, displayed_pin);
    assert(pairing_accept(&peer, NULL) == BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
    uint32_t old_request = displayed_request, old_pin = displayed_pin;
    pairing_failed(&peer, BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
    run_work(&window_expiry_work);
    mock_wait_hook = finish_disconnect;
    sender_step();
    assert(ui_pin_count == 2 && displayed_request != old_request && displayed_pin != old_pin);
    assert(pin_display_pending && !mock_advertising && !peer.refs);
    env_telemetry_ble_pairing_display_ready(old_request, 0);
    sender_step(); assert(!mock_advertising);
    env_telemetry_ble_pairing_display_ready(displayed_request, 0);
    sender_step(); assert(mock_advertising);
    connected(&peer, 0);
    assert(pairing_accept(&peer, NULL) == BT_SECURITY_ERR_SUCCESS);
    assert(app_passkey(&peer) == displayed_pin);
    peer.security = BT_SECURITY_L4;
    stored_bond = loaded_bond = true; /* SDK persisted BEFORE pairing_complete. */
    pairing_complete(&peer, true);
    assert(verify_bond_pending); sender_step();
    assert(!enrollment_until_ms && !pin_available && ui_closed_count == 1);
    int count_after_bond = notify_count;
    notify_latest(); assert(notify_count == count_after_bond + 1);
    mock_now = active_until_ms; run_work(&window_expiry_work);
    mock_wait_hook = finish_disconnect; mock_assert_drained = true;
    sender_step(); assert(!stack_enabled && !mock_stack_enabled && !peer.refs);
    mock_now += 60000;
    assert(!env_telemetry_ble_publish_frame(frame, sizeof(frame)));
    sender_step();
    assert(mock_advertising && stack_enabled && settings_load_count == 3 && loaded_bond);
    assert(ui_pin_count == 2 && !enrollment_until_ms && restore_count == 0);
    assert(auth_register_count == 1 && info_register_count == 1);
    assert(pairing_accept(&peer, NULL) == BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
    connected(&peer, 0); peer.security = 1;
    int count_before_encryption = notify_count;
    notify_latest(); assert(notify_count == count_before_encryption); /* Restored CCC cannot leak plaintext. */
    peer.security = BT_SECURITY_L4; security_changed(&peer, BT_SECURITY_L4, 0);
    notify_latest(); assert(notify_count == count_before_encryption + 1);
    assert(!env_telemetry_ble_repair());
    run_work(&window_expiry_work); mock_wait_hook = finish_disconnect;
    sender_step();
    assert(unpair_count == 1 && !stored_bond && ui_pin_count == 3 && pin_display_pending);
    env_telemetry_ble_pairing_display_ready(displayed_request, 0);
    sender_step(); connected(&peer, 0);
    assert(pairing_accept(&peer, NULL) == BT_SECURITY_ERR_SUCCESS);
    assert(app_passkey(&peer) == displayed_pin);
    /* Storage completion/callback queued across the physical window expiry
     * cannot reopen it or leave an unauthorized persisted owner. */
    mock_now = enrollment_until_ms; run_work(&window_expiry_work);
    stored_bond = loaded_bond = true; peer.security = BT_SECURITY_L4;
    pairing_complete(&peer, true);
    assert(discard_bond_pending && !enrollment_until_ms && !pin_available);
    mock_wait_hook = finish_disconnect; sender_step(); sender_step();
    assert(unpair_count == 2 && !stored_bond && !mock_stack_enabled && ui_pin_count == 3);
    assert(!env_telemetry_ble_repair()); sender_step();
    assert(ui_pin_count == 4 && pin_display_pending && unpair_count == 3);
    env_telemetry_ble_pairing_display_ready(displayed_request, -5);
    sender_step(); assert(!stack_enabled && !mock_advertising && !enrollment_until_ms);
    /* Minute publication after a display fault does not silently enroll. */
    mock_now += 60000; assert(!env_telemetry_ble_publish_frame(frame, sizeof(frame)));
    sender_step(); assert(ui_pin_count == 4);
    mock_now = active_until_ms; run_work(&window_expiry_work); sender_step();
    /* Corrupt/unreadable settings closes the enabled controller, then faults. */
    settings_error = -5; mock_now += 60000;
    assert(!env_telemetry_ble_publish_frame(frame, sizeof(frame))); sender_step();
    assert(stack_faulted && !mock_stack_enabled && !mock_advertising);
    puts("BLE security checks PASS: display before RF, one-use fresh PIN, stale completion ignored, L4 gate, persistent reload ordering, physical replacement, display/settings failure closed.");
}
