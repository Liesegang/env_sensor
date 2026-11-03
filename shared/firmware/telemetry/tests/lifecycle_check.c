#include "mock.h"
#include "../env_telemetry_ble.c"
static void run(struct k_work_delayable *w){w->pending=false;w->handler(&w->work);}
static uint8_t frame[24]={'O','E',1,24,24,0,0,0,0,0,0,0,1};
static void publish_during_disable(void){mock_disable_hook=NULL;frame[12]++;assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));}
static void expire_during_notify(void){mock_notify_hook=NULL;mock_now=active_until_ms;run(&window_expiry_work);}
static void lifecycle_during_disconnect(void){assert(expiry_disconnect_pending);int before=disable_count;update_stack_lifecycle();assert(disable_count==before&&stack_enabled);}
static void finish_graceful_disconnect(void){assert(mock_stack_enabled&&!stack_enabled);assert(disable_count==1);connection_callbacks.disconnected(mock_peer,22);connection_callbacks.recycled();}
static void finish_any_graceful_disconnect(void){assert(mock_stack_enabled&&!stack_enabled);connection_callbacks.disconnected(mock_peer,22);connection_callbacks.recycled();}
int main(void){
    assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    struct env_telemetry_ble_config conf={.active_window_ms=5000,.disable_between_windows=true,.advertising_interval_min=160,.advertising_interval_max=160};
    assert(!env_telemetry_ble_init(&conf));
    assert(started&&!stack_enabled&&identity_saved&&enable_count==1&&disable_count==1);
    assert(!mock_stack_enabled&&!mock_advertising&&!advertising_work.pending);
    mock_now=1000;assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    run(&advertising_work);assert(!stack_enabled&&!mock_advertising); // System handler only wakes normal thread.
    update_stack_lifecycle();start_advertising();
    assert(stack_enabled&&mock_stack_enabled&&mock_advertising&&restore_count==1&&active_until_ms==6000);
    mock_now=5999;assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    mock_now=6000;run(&window_expiry_work);assert(active_until_ms==10999&&disable_count==1);
    struct bt_conn peer={.subscribed=true};connected(&peer,0);assert(peer.refs==1);
    mock_disconnect_hook=lifecycle_during_disconnect;
    mock_notify_hook=expire_during_notify;notify_latest(); // Expiry releases a sender blocked on ATT allocation.
    mock_disconnect_hook=NULL;
    assert(active_until_ms==0&&disconnect_count==1&&disable_count==1&&peer.refs==1);
    mock_wait_hook=finish_graceful_disconnect;mock_assert_drained=true;
    update_stack_lifecycle();assert(!stack_enabled&&!mock_stack_enabled&&disable_count==2&&peer.refs==0);
    connected(&peer,0);assert(peer.refs==0); // Late queued event cannot add an app ref or send HCI after close.
    recycled();run(&advertising_work);start_advertising();assert(!mock_advertising&&start_count==1);
    mock_now=60000;assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    update_stack_lifecycle();start_advertising();assert(stack_enabled&&enable_count==3&&restore_count==2);
    mock_now=65000;run(&window_expiry_work);mock_disable_hook=publish_during_disable;
    update_stack_lifecycle();assert(!stack_enabled&&active_until_ms==70000&&!stack_faulted); // A concurrent publication survives teardown.
    update_stack_lifecycle();start_advertising();assert(stack_enabled&&enable_count==4&&restore_count==3);
    mock_now=70000;run(&window_expiry_work);update_stack_lifecycle();assert(!stack_enabled);
    mock_now=90000;assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));update_stack_lifecycle();start_advertising();connected(&peer,0);
    mock_now=95000;run(&window_expiry_work);int before=disable_count;update_stack_lifecycle();
    assert(disable_count==before+1&&mock_now==96000&&!stack_enabled&&!stack_faulted&&peer.refs==0); // Unresponsive peer gets a bounded 1 s grace, then force close.
    mock_now=100000;assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));update_stack_lifecycle();start_advertising();connected(&peer,0);
    mock_now=105000;before=disconnect_count;mock_wait_hook=finish_any_graceful_disconnect;mock_assert_drained=true;update_stack_lifecycle();
    assert(disconnect_count==before+1&&!stack_enabled&&active_until_ms==0&&!window_expiry_work.pending&&peer.refs==0); // Sender wins before expiry; it sends the termination request itself.
    mock_now=120000;assert(!env_telemetry_ble_publish_frame(frame,sizeof(frame)));
    mock_enable_error=-5;update_stack_lifecycle();assert(stack_faulted&&active_until_ms==0&&!stack_enabled);
    assert(env_telemetry_ble_publish_frame(frame,sizeof(frame))==-5);
    puts("BLE lifecycle checks PASS: boot disable, stable identity, thread-only restart, fresh deadline, expiry during notify, graceful cleanup before reset, bounded forced cleanup, concurrent publish, fail-closed.");
}
