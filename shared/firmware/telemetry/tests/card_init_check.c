/* Actual Card adapter, generated encoder and shared security transport. */
#define SECURE_CHECK 1
#include "mock.h"
#include "../env_telemetry_ble.c"
#include "../../../../card/firmware/src/card_ble.c"

static int opened;
static void clock_checks(void)
{
    struct card_ble_clock before;
    card_ble_clock_snapshot(&before);
    assert(before.boot_id && !before.revision && !card_ble_unix_seconds(0));
    uint8_t packet[12]={'O','T',1,0};
    uint64_t timestamp=1800000000123ULL;
    for(size_t i=0;i<8;++i)packet[4+i]=(uint8_t)(timestamp>>(8*i));
    assert(!configuration.set_clock_packet(packet,sizeof(packet),configuration.context));
    struct card_ble_clock clock;card_ble_clock_snapshot(&clock);
    assert(clock.revision==1&&clock.unix_anchor_s==1800000000&&clock.uptime_anchor_s==12);
    assert(card_ble_unix_seconds(0)==1799999988&&card_ble_unix_seconds(12999)==1800000000);
    packet[3]=1;
    assert(configuration.set_clock_packet(packet,sizeof(packet),configuration.context)<0);
    card_ble_clock_snapshot(&clock);assert(clock.revision==1);packet[3]=0;
    struct bt_conn peer={.security=3};
    assert(write_dedicated_clock(&peer,NULL,packet,12,0,0)==BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION));
    peer.security=4;connection=bt_conn_ref(&peer);stack_enabled=mock_stack_enabled=true;
    new_pin_pending=pin_display_pending=false;active_until_ms=mock_now+5000;
    assert(write_dedicated_clock(&peer,NULL,packet,12,0,0)==12);
    assert(clock_state.revision==2&&history_hold_until_ms>=mock_now+5000);
    uint8_t state[32],tail[12];
    assert(read_dedicated_clock(&peer,NULL,state,20,0)==20);
    /* A later correction must not mix the remaining bytes of a long read. */
    mock_now=15000;timestamp=1799999900456ULL;
    for(size_t i=0;i<8;++i)packet[4+i]=(uint8_t)(timestamp>>(8*i));
    assert(write_dedicated_clock(&peer,NULL,packet,12,0,0)==12);
    assert(read_dedicated_clock(&peer,NULL,tail,12,20)==12);
    memcpy(state+20,tail,12);assert(!memcmp(state,clock_read_snapshot,32));
    assert(card_ble_unix_seconds(0)==1799999885&&clock_state.revision==3);
    assert(read_dedicated_clock(&peer,NULL,state,32,0)==32&&state[0]=='C'&&state[1]=='T'&&state[3]==1);
    disconnected(&peer,19);assert(!peer.refs&&!history_hold_until_ms);
    puts("Card clock PASS: unknown, same-boot backfill, legacy/dedicated setter, backward correction, invalid unchanged, L4 and frozen long read");
}
void card_display_pairing_event(enum env_telemetry_pairing_event event,
                                unsigned int pin, uint32_t request, void *context)
{
    (void)pin; (void)request; (void)context;
    if (event == ENV_TELEMETRY_PAIRING_OPENED) { ++opened; }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    int expected = 0;
    bool reaches_enable = true;
    if (!strcmp(argv[1], "backend")) { settings_init_error = expected = -22; }
    else if (!strcmp(argv[1], "auth")) { auth_register_error = expected = -114; }
    else if (!strcmp(argv[1], "auth_info")) { info_register_error = expected = -22; }
    else if (!strcmp(argv[1], "enable")) { mock_enable_error = expected = -5; }
    else if (!strcmp(argv[1], "identity_save")) { identity_save_error = expected = -12; }
    else if (!strcmp(argv[1], "identity_missing_after_save")) { suppress_identity_save = true; expected = -5; }
    else if (!strcmp(argv[1], "identity_length")) { stored_identity_present = true; stored_identity_length = 1; expected = -5; }
    else if (!strcmp(argv[1], "identity_read")) { stored_identity_present = true; identity_read_error = -5; expected = -5; }
    else if (!strcmp(argv[1], "identity_mismatch")) { stored_identity_present = stored_identity_mismatch = true; expected = -5; }
    else if (!strcmp(argv[1], "identity_existing")) { stored_identity_present = true; }
    else { assert(!strcmp(argv[1], "normal")); }
    reaches_enable = !settings_init_error && !auth_register_error && !info_register_error;
    mock_now = 12345;
    assert(card_ble_init() == expected);
    /* Initial frame contains five honest -EAGAIN TLVs, no sensor value fields. */
    assert(latest_length == 54 && latest[6] == 0x4f && latest[7] == 0 && latest[8] == 0);
    const uint8_t ids[] = {1,2,3,4,7};
    for (size_t i = 0; i < sizeof(ids); ++i) {
        size_t offset = 24 + i * 6;
        assert(latest[offset] == ids[i] && latest[offset+2] == 2 && latest[offset+3] == 0);
        assert((int16_t)((uint16_t)latest[offset+4] | (uint16_t)latest[offset+5] << 8) == -EAGAIN);
    }
    if (!expected) {
        assert(started && !stack_enabled && new_pin_pending && opened == 1);
        assert(enable_count == 1 && disable_count == 1 && settings_load_count == 1);
        assert(configuration.pairing_event == card_display_pairing_event);
        assert(configuration.pairing_window_ms == 180000 && configuration.active_window_ms == 5000);
        /* A factory address is not automatically persisted by this SDK.
         * Save exactly once initially, then reload without a flash write. */
        int expected_saves = !strcmp(argv[1], "identity_existing") ? 0 : 1;
        assert(stored_identity_present && identity_save_count == expected_saves);
        assert(!enable_with_settings() && identity_save_count == expected_saves);
        assert(!bt_disable());
        if (!strcmp(argv[1], "normal")) { clock_checks(); }
    } else {
        assert(!started && !opened && !mock_stack_enabled && !mock_advertising);
        assert(enable_count == (reaches_enable ? 1 : 0));
        assert(disable_count == (reaches_enable && !mock_enable_error ? 1 : 0));
        /* Invalid existing records must fail closed without being overwritten. */
        if (stored_identity_present) { assert(!identity_save_count); }
    }
    printf("Card actual init integration PASS: %s\n", argv[1]);
}
