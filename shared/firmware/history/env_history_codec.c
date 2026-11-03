#include "env_history.h"
#include "env_clock.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define VALID_MASK (ENV_HISTORY_VALID_TEMPERATURE | ENV_HISTORY_VALID_HUMIDITY | \
    ENV_HISTORY_VALID_PRESSURE | ENV_HISTORY_VALID_CO2 | ENV_HISTORY_VALID_LUX | \
    ENV_HISTORY_VALID_BME_TEMPERATURE | ENV_HISTORY_VALID_BME_HUMIDITY | \
    ENV_HISTORY_VALID_BME_GAS | ENV_HISTORY_VALID_VOC_RAW | ENV_HISTORY_VALID_NOX_RAW | \
    ENV_HISTORY_VALID_VOC_INDEX | ENV_HISTORY_VALID_NOX_INDEX | ENV_HISTORY_VALID_HCHO)
#define FLAG_MASK (ENV_HISTORY_FLAG_MORE | ENV_HISTORY_FLAG_GAP)

static void put_le(uint8_t *output, uint64_t value, size_t size)
{
    for (size_t i = 0; i < size; ++i) { output[i] = (uint8_t)(value >> (8 * i)); }
}

static uint64_t get_le(const uint8_t *input, size_t size)
{
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) { value |= (uint64_t)input[i] << (8 * i); }
    return value;
}

static bool utc_valid(uint64_t utc)
{
    return utc == 0 || (utc >= ENV_HISTORY_UTC_MINIMUM && utc <= ENV_HISTORY_UTC_MAXIMUM);
}

static bool cursor_valid(struct env_history_cursor cursor)
{
    return (cursor.boot_id == 0) == (cursor.sequence == 0);
}

static int record_valid(const struct env_history_record *record)
{
    if (!record || record->sequence == 0 || !utc_valid(record->unix_s) ||
        (record->valid & ~VALID_MASK) || record->gas_valid > 1 || record->heater_stable > 1) {
        return -EINVAL;
    }
    if ((record->valid & (ENV_HISTORY_VALID_TEMPERATURE | ENV_HISTORY_VALID_HUMIDITY)) &&
        record->rht_source > ENV_HISTORY_RHT_BME69X) { return -EINVAL; }
    return 0;
}

uint32_t env_history_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_MAX;
}

int env_history_encode_record(const struct env_history_record *record,
                              uint8_t *output, size_t capacity)
{
    int error = record_valid(record);
    if (error || !output) { return error ? error : -EINVAL; }
    if (capacity < ENV_HISTORY_RECORD_SIZE) { return -ENOSPC; }
    memset(output, 0, ENV_HISTORY_RECORD_SIZE);
    put_le(output + ENV_HISTORY_RECORD_SEQUENCE_OFFSET, record->sequence, 4);
    put_le(output + ENV_HISTORY_RECORD_UPTIME_S_OFFSET, record->uptime_s, 4);
    put_le(output + ENV_HISTORY_RECORD_UNIX_S_OFFSET, record->unix_s, 8);
#define FIELD(member, tag, size, mask) \
    put_le(output + ENV_HISTORY_RECORD_##tag##_OFFSET, \
           (record->valid & (mask)) ? (uint64_t)record->member : 0, size)
    FIELD(temperature_mc, TEMPERATURE_MC, 4, ENV_HISTORY_VALID_TEMPERATURE);
    FIELD(humidity_mpercent, HUMIDITY_MPERCENT, 4, ENV_HISTORY_VALID_HUMIDITY);
    FIELD(hcho_millippb, HCHO_MILLIPPB, 4, ENV_HISTORY_VALID_HCHO);
    FIELD(bme_temperature_mc, BME_TEMPERATURE_MC, 4, ENV_HISTORY_VALID_BME_TEMPERATURE);
    FIELD(bme_humidity_mpercent, BME_HUMIDITY_MPERCENT, 4, ENV_HISTORY_VALID_BME_HUMIDITY);
    FIELD(pressure_pa, PRESSURE_PA, 4, ENV_HISTORY_VALID_PRESSURE);
    FIELD(gas_resistance_ohm, GAS_RESISTANCE_OHM, 4, ENV_HISTORY_VALID_BME_GAS);
    FIELD(millilux, MILLILUX, 4, ENV_HISTORY_VALID_LUX);
    FIELD(co2_ppm, CO2_PPM, 2, ENV_HISTORY_VALID_CO2);
    FIELD(raw_voc, RAW_VOC, 2, ENV_HISTORY_VALID_VOC_RAW);
    FIELD(raw_nox, RAW_NOX, 2, ENV_HISTORY_VALID_NOX_RAW);
    FIELD(voc_index, VOC_INDEX, 2, ENV_HISTORY_VALID_VOC_INDEX);
    FIELD(nox_index, NOX_INDEX, 2, ENV_HISTORY_VALID_NOX_INDEX);
#undef FIELD
    put_le(output + ENV_HISTORY_RECORD_STCC_STATUS_OFFSET, record->stcc_status, 2);
    put_le(output + ENV_HISTORY_RECORD_STCC_SELF_TEST_OFFSET, record->stcc_self_test, 2);
    put_le(output + ENV_HISTORY_RECORD_VALID_OFFSET, record->valid, 2);
    output[ENV_HISTORY_RECORD_RHT_SOURCE_OFFSET] =
        (record->valid & (ENV_HISTORY_VALID_TEMPERATURE | ENV_HISTORY_VALID_HUMIDITY)) ? record->rht_source : 0;
    output[ENV_HISTORY_RECORD_GAS_VALID_OFFSET] = record->gas_valid;
    output[ENV_HISTORY_RECORD_HEATER_STABLE_OFFSET] = record->heater_stable;
    return ENV_HISTORY_RECORD_SIZE;
}

int env_history_encode_page(const struct env_history_page *page,
                            const struct env_history_record *records, size_t count,
                            uint8_t *output, size_t capacity)
{
    if (!page || !records || !output || page->request_id == 0 || page->boot_id == 0 ||
        (page->flags & ~FLAG_MASK) || count == 0 || count > ENV_HISTORY_MAX_RECORDS) { return -EINVAL; }
    size_t length = ENV_HISTORY_PAGE_SIZE + count * ENV_HISTORY_RECORD_SIZE;
    if (capacity < length) { return -ENOSPC; }
    uint8_t flags = page->flags;
    for (size_t i = 0; i < count; ++i) {
        int error = record_valid(&records[i]);
        if (error) { return error; }
        if (i > 0) {
            if (records[i].sequence <= records[i - 1].sequence ||
                records[i].uptime_s < records[i - 1].uptime_s) { return -EINVAL; }
            if (records[i].sequence - records[i - 1].sequence != 1) { flags |= ENV_HISTORY_FLAG_GAP; }
        }
    }
    memset(output, 0, length);
    output[0] = ENV_HISTORY_PAGE_MAGIC_0;
    output[1] = ENV_HISTORY_PAGE_MAGIC_1;
    output[ENV_HISTORY_PAGE_VERSION_OFFSET] = ENV_HISTORY_VERSION;
    output[ENV_HISTORY_PAGE_FLAGS_OFFSET] = flags;
    put_le(output + ENV_HISTORY_PAGE_REQUEST_ID_OFFSET, page->request_id, 4);
    put_le(output + ENV_HISTORY_PAGE_BOOT_ID_OFFSET, page->boot_id, 8);
    put_le(output + ENV_HISTORY_PAGE_FIRST_SEQUENCE_OFFSET, records[0].sequence, 4);
    put_le(output + ENV_HISTORY_PAGE_RECORD_COUNT_OFFSET, count, 2);
    put_le(output + ENV_HISTORY_PAGE_RECORD_SIZE_OFFSET, ENV_HISTORY_RECORD_SIZE, 2);
    put_le(output + ENV_HISTORY_PAGE_CLOCK_REVISION_OFFSET, page->clock_revision, 4);
    for (size_t i = 0; i < count; ++i) {
        /* The full input was validated before writing any byte. */
        (void)env_history_encode_record(&records[i],
            output + ENV_HISTORY_PAGE_SIZE + i * ENV_HISTORY_RECORD_SIZE, ENV_HISTORY_RECORD_SIZE);
    }
    uint32_t crc = env_history_crc32(output, length);
    put_le(output + ENV_HISTORY_PAGE_CRC32_OFFSET, crc, 4);
    return (int)length;
}

int env_history_decode_control(const uint8_t *bytes, size_t length,
                               struct env_history_control *output)
{
    if (!bytes || !output) { return -EINVAL; }
    if (length != ENV_HISTORY_CONTROL_SIZE) { return -EMSGSIZE; }
    if (bytes[0] != ENV_HISTORY_CONTROL_MAGIC_0 || bytes[1] != ENV_HISTORY_CONTROL_MAGIC_1 ||
        bytes[ENV_HISTORY_CONTROL_VERSION_OFFSET] != ENV_HISTORY_VERSION) { return -EBADMSG; }
    struct env_history_control parsed = {
        .opcode = bytes[ENV_HISTORY_CONTROL_OPCODE_OFFSET],
        .request_id = (uint32_t)get_le(bytes + ENV_HISTORY_CONTROL_REQUEST_ID_OFFSET, 4),
        .cursor = {
            .boot_id = get_le(bytes + ENV_HISTORY_CONTROL_CURSOR_BOOT_ID_OFFSET, 8),
            .sequence = (uint32_t)get_le(bytes + ENV_HISTORY_CONTROL_CURSOR_SEQUENCE_OFFSET, 4),
        },
    };
    if (!parsed.request_id || !cursor_valid(parsed.cursor)) { return -EBADMSG; }
    switch (parsed.opcode) {
    case ENV_HISTORY_OP_PULL:
        break;
    case ENV_HISTORY_OP_ACK:
        if (!parsed.cursor.boot_id) { return -EBADMSG; }
        break;
    case ENV_HISTORY_OP_CANCEL:
        if (parsed.cursor.boot_id) { return -EBADMSG; }
        break;
    default:
        return -EBADMSG;
    }
    *output = parsed;
    return ENV_HISTORY_CONTROL_SIZE;
}

static bool request_valid(uint8_t state, uint32_t request_id)
{
    return state <= ENV_HISTORY_STATE_ERROR && (state == ENV_HISTORY_STATE_IDLE || request_id != 0);
}

int env_history_encode_status(const struct env_history_status *status,
                              uint8_t *output, size_t capacity)
{
    if (!status || !output || !request_valid(status->state, status->request_id) ||
        !cursor_valid(status->earliest) || !cursor_valid(status->latest) ||
        ((status->earliest.boot_id == 0) != (status->latest.boot_id == 0)) ||
        (status->flags & ~FLAG_MASK) || status->current_boot_id == 0 ||
        !utc_valid(status->utc_anchor_s)) { return -EINVAL; }
    if ((status->clock_revision == 0 && (status->utc_anchor_s || status->uptime_anchor_s)) ||
        (status->clock_revision != 0 && status->utc_anchor_s == 0)) { return -EINVAL; }
    if ((status->state == ENV_HISTORY_STATE_ERROR && status->error >= 0) ||
        (status->state != ENV_HISTORY_STATE_ERROR && status->error != 0)) { return -EINVAL; }
    if (status->state == ENV_HISTORY_STATE_READY) {
        if (!status->page_record_count || status->page_record_count > ENV_HISTORY_MAX_RECORDS ||
            status->page_size != ENV_HISTORY_PAGE_SIZE + status->page_record_count * ENV_HISTORY_RECORD_SIZE ||
            status->page_last_sequence == 0) { return -EINVAL; }
    } else if (status->page_record_count || status->page_size || status->page_last_sequence) { return -EINVAL; }
    if (capacity < ENV_HISTORY_STATUS_SIZE) { return -ENOSPC; }
    memset(output, 0, ENV_HISTORY_STATUS_SIZE);
    output[0] = ENV_HISTORY_STATUS_MAGIC_0;
    output[1] = ENV_HISTORY_STATUS_MAGIC_1;
    output[ENV_HISTORY_STATUS_VERSION_OFFSET] = ENV_HISTORY_VERSION;
    output[ENV_HISTORY_STATUS_STATE_OFFSET] = status->state;
#define STATUS(member, tag, size) put_le(output + ENV_HISTORY_STATUS_##tag##_OFFSET, \
                                       (uint64_t)status->member, size)
    STATUS(request_id, REQUEST_ID, 4);
    STATUS(earliest.boot_id, EARLIEST_BOOT_ID, 8);
    STATUS(earliest.sequence, EARLIEST_SEQUENCE, 4);
    STATUS(latest.boot_id, LATEST_BOOT_ID, 8);
    STATUS(latest.sequence, LATEST_SEQUENCE, 4);
    STATUS(flags, FLAGS, 4);
    STATUS(page_record_count, PAGE_RECORD_COUNT, 2);
    STATUS(page_size, PAGE_SIZE, 2);
    STATUS(page_last_sequence, PAGE_LAST_SEQUENCE, 4);
    STATUS(error, ERROR, 4);
    STATUS(current_boot_id, CURRENT_BOOT_ID, 8);
    STATUS(clock_revision, CLOCK_REVISION, 4);
    STATUS(utc_anchor_s, UTC_ANCHOR_S, 8);
    STATUS(uptime_anchor_s, UPTIME_ANCHOR_S, 4);
#undef STATUS
    return ENV_HISTORY_STATUS_SIZE;
}

int env_history_encode_hint(uint8_t state, uint32_t request_id,
                            uint8_t *output, size_t capacity)
{
    if (!output || !request_valid(state, request_id)) { return -EINVAL; }
    if (capacity < ENV_HISTORY_HINT_SIZE) { return -ENOSPC; }
    output[0] = ENV_HISTORY_HINT_MAGIC_0;
    output[1] = ENV_HISTORY_HINT_MAGIC_1;
    output[ENV_HISTORY_HINT_VERSION_OFFSET] = ENV_HISTORY_VERSION;
    output[ENV_HISTORY_HINT_STATE_OFFSET] = state;
    put_le(output + ENV_HISTORY_HINT_REQUEST_ID_OFFSET, request_id, 4);
    return ENV_HISTORY_HINT_SIZE;
}

int env_clock_encode_state(const struct env_clock_state *state,
                           uint8_t *output, size_t capacity)
{
    if (!state || !output || state->boot_id == 0 ||
        (state->flags & ~ENV_CLOCK_FLAG_KNOWN)) { return -EINVAL; }
    if (state->flags & ENV_CLOCK_FLAG_KNOWN) {
        if (state->clock_revision == 0 || state->unix_anchor_ms < ENV_CLOCK_UTC_MINIMUM_MS ||
            state->unix_anchor_ms > ENV_CLOCK_UTC_MAXIMUM_MS) { return -EINVAL; }
    } else if (state->clock_revision || state->unix_anchor_ms || state->uptime_anchor_ms) { return -EINVAL; }
    if (capacity < ENV_CLOCK_STATE_SIZE) { return -ENOSPC; }
    output[0] = ENV_CLOCK_STATE_MAGIC_0;
    output[1] = ENV_CLOCK_STATE_MAGIC_1;
    output[ENV_CLOCK_STATE_VERSION_OFFSET] = ENV_CLOCK_VERSION;
    output[ENV_CLOCK_STATE_FLAGS_OFFSET] = state->flags;
    put_le(output + ENV_CLOCK_STATE_BOOT_ID_OFFSET, state->boot_id, 8);
    put_le(output + ENV_CLOCK_STATE_CLOCK_REVISION_OFFSET, state->clock_revision, 4);
    put_le(output + ENV_CLOCK_STATE_UNIX_ANCHOR_MS_OFFSET, state->unix_anchor_ms, 8);
    put_le(output + ENV_CLOCK_STATE_UPTIME_ANCHOR_MS_OFFSET, state->uptime_anchor_ms, 8);
    return ENV_CLOCK_STATE_SIZE;
}

int env_clock_decode_write(const uint8_t *bytes, size_t length, uint64_t *unix_ms)
{
    if (!bytes || !unix_ms) { return -EINVAL; }
    if (length != ENV_CLOCK_WRITE_SIZE) { return -EMSGSIZE; }
    if (bytes[0] != ENV_CLOCK_WRITE_MAGIC_0 || bytes[1] != ENV_CLOCK_WRITE_MAGIC_1 ||
        bytes[ENV_CLOCK_WRITE_VERSION_OFFSET] != ENV_CLOCK_VERSION ||
        bytes[ENV_CLOCK_WRITE_RESERVED_OFFSET] != 0) { return -EBADMSG; }
    uint64_t parsed = get_le(bytes + ENV_CLOCK_WRITE_UNIX_MS_OFFSET, 8);
    if (parsed < ENV_CLOCK_UTC_MINIMUM_MS || parsed > ENV_CLOCK_UTC_MAXIMUM_MS) { return -EBADMSG; }
    *unix_ms = parsed;
    return ENV_CLOCK_WRITE_SIZE;
}
