#include "env_history.h"
#include "env_clock.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void untouched(const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) { assert(bytes[i] == 0xa7); }
}

static void dump(const char *name, const uint8_t *bytes, size_t count)
{
    printf("%s=", name);
    for (size_t i = 0; i < count; ++i) { printf("%02x", bytes[i]); }
    puts("");
}

int main(void)
{
    assert(env_history_crc32((const uint8_t *)"123456789", 9) == UINT32_C(0xcbf43926));
    assert(env_history_crc32(NULL, 0) == 0);
    struct env_history_record records[6];
    for (size_t i = 0; i < 6; ++i) {
        records[i] = (struct env_history_record){
            .sequence = (uint32_t)(1001 + i), .uptime_s = (uint32_t)(300 + i),
            .unix_s = UINT64_C(1780000000) + i,
            .temperature_mc = -12345, .humidity_mpercent = 45678,
            .hcho_millippb = -123, .bme_temperature_mc = -23456,
            .bme_humidity_mpercent = 67890, .pressure_pa = 100123,
            .gas_resistance_ohm = 1234567, .millilux = UINT32_C(0x12345678),
            .co2_ppm = 1234, .raw_voc = 50000, .raw_nox = UINT16_MAX,
            .voc_index = 321, .nox_index = 123, .stcc_status = 0xabcd,
            .stcc_self_test = 0x55aa, .valid = 0x1fff,
            .rht_source = 1, .gas_valid = 1, .heater_stable = 0, .reserved = 0xfe,
        };
    }
    uint8_t record[68];
    assert(env_history_encode_record(&records[0], record, sizeof(record)) == 68);
    dump("record", record, sizeof(record));
    records[0].unix_s = 0;
    assert(env_history_encode_record(&records[0], record, sizeof(record)) == 68);
    dump("unknown_time", record, sizeof(record));
    records[0].unix_s = UINT64_C(1780000000);
    struct env_history_record missing = records[0];
    missing.valid = ENV_HISTORY_VALID_TEMPERATURE | ENV_HISTORY_VALID_VOC_RAW;
    assert(env_history_encode_record(&missing, record, sizeof(record)) == 68);
    dump("canonical", record, sizeof(record));
    /* Reading the same record from an empty CSV field yields zero rather than
     * stale sensor memory. Their canonical bytes must match exactly. */
    struct env_history_record from_csv = missing;
    from_csv.humidity_mpercent = 0; from_csv.hcho_millippb = 0;
    from_csv.bme_temperature_mc = 0; from_csv.bme_humidity_mpercent = 0;
    from_csv.pressure_pa = 0; from_csv.gas_resistance_ohm = 0; from_csv.millilux = 0;
    from_csv.co2_ppm = 0; from_csv.raw_nox = 0; from_csv.voc_index = 0; from_csv.nox_index = 0;
    from_csv.reserved = 0;
    uint8_t comparison[68];
    assert(env_history_encode_record(&from_csv, comparison, sizeof(comparison)) == 68);
    assert(!memcmp(record, comparison, sizeof(record)));
    missing.valid = 0;
    missing.rht_source = 255;
    assert(env_history_encode_record(&missing, record, sizeof(record)) == 68);
    assert(record[64] == 0 && record[67] == 0);
    missing = records[0];
    missing.sequence = UINT32_MAX;
    missing.uptime_s = UINT32_MAX;
    missing.temperature_mc = INT32_MIN;
    missing.co2_ppm = INT16_MIN;
    missing.humidity_mpercent = UINT32_MAX;
    missing.millilux = UINT32_MAX;
    assert(env_history_encode_record(&missing, record, sizeof(record)) == 68);
    assert(record[16] == 0 && record[19] == 0x80);
    assert(record[48] == 0 && record[49] == 0x80 && record[44] == 255);
    dump("signed_limits", record, sizeof(record));

    struct env_history_page page = {
        .request_id = UINT32_C(0x12345678), .boot_id = UINT64_C(0x9abcdef012345678),
        .clock_revision = 7, .flags = ENV_HISTORY_FLAG_MORE,
    };
    uint8_t bytes[440];
    assert(env_history_encode_page(&page, records, 6, bytes, sizeof(bytes)) == 440);
    dump("page", bytes, sizeof(bytes));
    memset(bytes, 0xa7, sizeof(bytes));
    assert(env_history_encode_page(&page, records, 6, bytes, sizeof(bytes) - 1) == -ENOSPC);
    untouched(bytes, sizeof(bytes));
    records[5].valid |= 0x8000;
    assert(env_history_encode_page(&page, records, 6, bytes, sizeof(bytes)) == -EINVAL);
    untouched(bytes, sizeof(bytes));
    records[5].valid = 0x1fff;
    records[5].sequence = records[4].sequence;
    assert(env_history_encode_page(&page, records, 6, bytes, sizeof(bytes)) == -EINVAL);
    untouched(bytes, sizeof(bytes));
    records[5].sequence = 1007;
    assert(env_history_encode_page(&page, records, 6, bytes, sizeof(bytes)) == 440);
    assert(bytes[3] == (ENV_HISTORY_FLAG_MORE | ENV_HISTORY_FLAG_GAP));
    dump("page_gap", bytes, sizeof(bytes));
    memset(bytes, 0xa7, sizeof(bytes));
    assert(env_history_encode_page(&page, records, 0, bytes, sizeof(bytes)) == -EINVAL);
    assert(env_history_encode_page(&page, records, 7, bytes, sizeof(bytes)) == -EINVAL);
    untouched(bytes, sizeof(bytes));

    uint8_t command[20] = {0x48, 0x43, 1, 1, 0x78, 0x56, 0x34, 0x12};
    struct env_history_control parsed;
    assert(env_history_decode_control(command, 20, &parsed) == 20);
    assert(parsed.request_id == page.request_id && parsed.cursor.boot_id == 0);
    dump("pull_oldest", command, sizeof(command));
    command[3] = 2; command[8] = 0x11; command[16] = 0x22;
    assert(env_history_decode_control(command, 20, &parsed) == 20);
    assert(parsed.cursor.boot_id == 0x11 && parsed.cursor.sequence == 0x22);
    dump("ack", command, sizeof(command));
    struct env_history_control prior = parsed;
    command[16] = 0;
    assert(env_history_decode_control(command, 20, &parsed) == -EBADMSG);
    assert(!memcmp(&prior, &parsed, sizeof(prior)));
    command[3] = 3;
    memset(command + 8, 0, 12);
    assert(env_history_decode_control(command, 20, &parsed) == 20);
    dump("cancel", command, sizeof(command));
    for (size_t length = 0; length < 20; ++length) {
        assert(env_history_decode_control(command, length, &parsed) == -EMSGSIZE);
    }
    command[2] = 2;
    assert(env_history_decode_control(command, 20, &parsed) == -EBADMSG);
    command[2] = 1; command[3] = 99;
    assert(env_history_decode_control(command, 20, &parsed) == -EBADMSG);
    uint8_t pull_after[20] = {0x48, 0x43, 1, 1, 0xd4, 0xc3, 0xb2, 0xa1,
        0x21, 0x43, 0x65, 0x87, 0xa9, 0xcb, 0xed, 0x8f, 0xff, 0xff, 0xff, 0xff};
    assert(env_history_decode_control(pull_after, 20, &parsed) == 20);
    assert(parsed.cursor.boot_id == UINT64_C(0x8fedcba987654321));
    assert(parsed.cursor.sequence == UINT32_MAX);
    dump("pull_after", pull_after, sizeof(pull_after));

    struct env_history_status status = {
        .state = ENV_HISTORY_STATE_READY, .request_id = page.request_id,
        .earliest = {.boot_id = UINT64_C(0x8000000000000001), .sequence = 1},
        .latest = {.boot_id = UINT64_C(0x1122334455667788), .sequence = 123456},
        .flags = ENV_HISTORY_FLAG_MORE | ENV_HISTORY_FLAG_GAP,
        .page_record_count = 6, .page_size = 440, .page_last_sequence = 1006,
        .current_boot_id = UINT64_C(0x1122334455667788), .clock_revision = 7,
        .utc_anchor_s = UINT64_C(1780000300), .uptime_anchor_s = 600,
    };
    uint8_t status_bytes[72], hint[8];
    assert(env_history_encode_status(&status, status_bytes, sizeof(status_bytes)) == 72);
    dump("status", status_bytes, sizeof(status_bytes));
    assert(env_history_encode_hint(status.state, status.request_id, hint, sizeof(hint)) == 8);
    dump("hint", hint, sizeof(hint));
    status.state = ENV_HISTORY_STATE_COMPLETE;
    memset(status_bytes, 0xa7, sizeof(status_bytes));
    assert(env_history_encode_status(&status, status_bytes, sizeof(status_bytes)) == -EINVAL);
    untouched(status_bytes, sizeof(status_bytes));
    status.page_record_count = 0; status.page_size = 0; status.page_last_sequence = 0;
    status.state = ENV_HISTORY_STATE_ERROR; status.error = -5;
    assert(env_history_encode_status(&status, status_bytes, sizeof(status_bytes)) == 72);
    dump("error_status", status_bytes, sizeof(status_bytes));
    status.error = 0;
    assert(env_history_encode_status(&status, status_bytes, sizeof(status_bytes)) == -EINVAL);
    status.state = ENV_HISTORY_STATE_IDLE; status.clock_revision = 0;
    status.utc_anchor_s = 0; status.uptime_anchor_s = 0;
    assert(env_history_encode_status(&status, status_bytes, sizeof(status_bytes)) == 72);
    struct env_clock_state clock = {
        .flags = ENV_CLOCK_FLAG_KNOWN, .boot_id = UINT64_C(0x1122334455667788),
        .clock_revision = 7, .unix_anchor_ms = UINT64_C(1780000300000),
        .uptime_anchor_ms = 600000,
    };
    uint8_t clock_bytes[32];
    assert(env_clock_encode_state(&clock, clock_bytes, sizeof(clock_bytes)) == 32);
    dump("clock_state", clock_bytes, sizeof(clock_bytes));
    memset(clock_bytes, 0xa7, sizeof(clock_bytes));
    assert(env_clock_encode_state(&clock, clock_bytes, sizeof(clock_bytes) - 1) == -ENOSPC);
    untouched(clock_bytes, sizeof(clock_bytes));
    clock.flags = 0;
    assert(env_clock_encode_state(&clock, clock_bytes, sizeof(clock_bytes)) == -EINVAL);
    untouched(clock_bytes, sizeof(clock_bytes));
    clock.clock_revision = 0; clock.unix_anchor_ms = 0; clock.uptime_anchor_ms = 0;
    assert(env_clock_encode_state(&clock, clock_bytes, sizeof(clock_bytes)) == 32);
    dump("clock_unknown", clock_bytes, sizeof(clock_bytes));
    uint8_t clock_write[12] = {0x4f, 0x54, 1, 0};
    uint64_t written = UINT64_C(1780000300123), parsed_unix = 0;
    for (size_t i = 0; i < 8; ++i) { clock_write[4 + i] = (uint8_t)(written >> (8 * i)); }
    assert(env_clock_decode_write(clock_write, sizeof(clock_write), &parsed_unix) == 12);
    assert(parsed_unix == written);
    dump("clock_write", clock_write, sizeof(clock_write));
    clock_write[3] = 1;
    assert(env_clock_decode_write(clock_write, sizeof(clock_write), &parsed_unix) == -EBADMSG);
    assert(parsed_unix == written);
    clock_write[3] = 0;
    for (size_t length = 0; length < 12; ++length) {
        assert(env_clock_decode_write(clock_write, length, &parsed_unix) == -EMSGSIZE);
    }
    memset(clock_write + 4, 0, 8);
    assert(env_clock_decode_write(clock_write, sizeof(clock_write), &parsed_unix) == -EBADMSG);
    assert(parsed_unix == written);
    return 0;
}
