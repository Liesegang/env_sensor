#include "mock.h"
#include "../env_telemetry_ble.c"
static void run(struct k_work_delayable *work) {work->pending=false;work->handler(&work->work);}
static uint8_t frame[24]={'O','E',1,24,24,0,0,0,0,0,0,0,1};
int main(void) {
    assert(env_telemetry_ble_publish_frame(frame,sizeof(frame))==0);
    assert(!advertising_work.pending && !window_expiry_work.pending);
    struct env_telemetry_ble_config config={.active_window_ms=5000};
    assert(env_telemetry_ble_init(&config)==0);
    assert(!advertising_work.pending && !mock_advertising); // Initial errors do not open radio.
    mock_now=1000;
    assert(env_telemetry_ble_publish_frame(frame,sizeof(frame))==0);
    assert(active_until_ms==6000 && advertising_work.pending && window_expiry_work.due==6000);
    run(&advertising_work);assert(mock_advertising && start_count==1);
    mock_now=5999;
    expire_window(NULL);assert(mock_advertising && window_expiry_work.due==6000);
    assert(env_telemetry_ble_publish_frame(frame,sizeof(frame))==0);
    assert(active_until_ms==10999);
    mock_now=6000;run(&window_expiry_work);
    assert(mock_advertising && stop_count==0 && window_expiry_work.due==10999); // Stale timeout respects new deadline.
    mock_now=10999;run(&window_expiry_work);
    assert(!mock_advertising && active_until_ms==0 && !advertising_work.pending);
    recycled();assert(!advertising_work.pending); // Recycle cannot resume a paused radio.
    advertise(NULL);assert(start_count==1); // Stale advertise work is harmless.
    struct bt_conn peer={0};connected(&peer,0);assert(disconnect_count==1); // Late connection is closed.
    disconnected(&peer,19);assert(peer.refs==0);
    mock_now=60000;assert(env_telemetry_ble_publish_frame(frame,sizeof(frame))==0);
    run(&advertising_work);connected(&peer,0);assert(peer.refs==1);
    mock_now=65000;run(&window_expiry_work);
    assert(disconnect_count==2 && !mock_advertising && active_until_ms==0);
    disconnected(&peer,19);recycled();assert(!advertising_work.pending && peer.refs==0);
    started=false;configuration.active_window_ms=0;
    assert(env_telemetry_ble_init(NULL)==0 && advertising_work.pending);
    run(&advertising_work);assert(mock_advertising);
    mock_now=100000;expire_window(NULL);assert(mock_advertising); // Outdoor continuous mode is unchanged.
    recycled();assert(advertising_work.pending);
    puts("BLE window checks PASS: paused init, publication wake, fresh deadline, stale work, late peer, expiry disconnect, continuous mode.");
}
