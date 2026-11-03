#include "mock.h"
#include "../env_telemetry_ble.c"
static void tick_fragment(void) {
    mock_now+=100;
    if(mock_now>=5000)expire_window(&window_expiry_work.work);
    assert(active_until_ms>mock_now&&disconnect_count==0);
}
int main(void) {
    uint8_t frame[146]={'O','E',1,24,146};
    assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    struct env_telemetry_ble_config conf={.active_window_ms=5000};
    assert(!env_telemetry_ble_init(&conf));
    assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    struct bt_conn peer={.subscribed=true};connected(&peer,0);
    mock_mtu=23;mock_now=4900;mock_notify_observer=tick_fragment;
    notify_latest();
    assert(notify_count==19&&mock_now==6800&&active_until_ms==9900);
    mock_notify_observer=NULL;
    assert(!env_telemetry_ble_hold_connection(&peer,120000));
    int64_t image_deadline=active_until_ms;
    mock_now+=60000;
    assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    assert(active_until_ms==image_deadline&&transfer_hold_until_ms==image_deadline);
    mock_now=image_deadline-100;
    notify_latest();
    assert(active_until_ms==mock_now+5000&&notify_hold_until_ms==active_until_ms);
    env_telemetry_ble_release_connection(&peer);
    assert(!transfer_hold_until_ms&&active_until_ms==notify_hold_until_ms);
    assert(env_telemetry_ble_hold_connection(NULL,1000)==-EINVAL);
    assert(env_telemetry_ble_hold_connection(&peer,120001)==-EINVAL);
    assert(!env_telemetry_ble_hold_connection(&peer,120000));
    mock_now+=1000;
    assert(!env_telemetry_ble_hold_history(&peer,120000));
    assert(env_telemetry_ble_hold_history(&peer,300001)==-EINVAL);
    assert(!env_telemetry_ble_set_history_lease(&peer,300000));
    assert(history_hold_until_ms==mock_now+300000&&active_until_ms==history_hold_until_ms);
    assert(!env_telemetry_ble_set_history_lease(&peer,120000));
    assert(history_hold_until_ms==mock_now+120000&&active_until_ms==history_hold_until_ms);
    int64_t history_deadline=history_hold_until_ms;
    env_telemetry_ble_release_connection(&peer);
    assert(!transfer_hold_until_ms&&history_hold_until_ms==history_deadline&&active_until_ms==history_deadline);
    assert(!env_telemetry_ble_hold_connection(&peer,120000));
    int64_t retained_image=transfer_hold_until_ms;
    env_telemetry_ble_release_history(&peer);
    assert(!history_hold_until_ms&&transfer_hold_until_ms==retained_image&&active_until_ms==retained_image);
    assert(!env_telemetry_ble_hold_history(&peer,120000));
    disconnected(&peer,19);
    assert(!transfer_hold_until_ms&&!history_hold_until_ms&&!notify_hold_until_ms&&peer.refs==0);
    assert(active_until_ms==mock_now+5000);
    assert(env_telemetry_ble_hold_connection(&peer,1000)==-ENOTCONN);
    puts("BLE holds PASS: MTU23 full snapshot near expiry, minute publish preserves image lease, independent release, disconnect clears all leases.");
}
