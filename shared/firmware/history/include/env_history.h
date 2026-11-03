#ifndef ENV_HISTORY_H_
#define ENV_HISTORY_H_

#include <stddef.h>
#include <stdint.h>
#include "env_history_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

struct env_history_cursor {
    uint64_t boot_id;
    uint32_t sequence;
};

struct env_history_control {
    uint8_t opcode;
    uint32_t request_id;
    struct env_history_cursor cursor;
};

/* In-memory layout is not the wire layout. Use the codec for every field. */
struct env_history_record {
    uint32_t sequence;
    uint32_t uptime_s;
    uint64_t unix_s;
    int32_t temperature_mc;
    uint32_t humidity_mpercent;
    int32_t hcho_millippb;
    int32_t bme_temperature_mc;
    uint32_t bme_humidity_mpercent;
    uint32_t pressure_pa;
    uint32_t gas_resistance_ohm;
    uint32_t millilux;
    int16_t co2_ppm;
    uint16_t raw_voc;
    uint16_t raw_nox;
    uint16_t voc_index;
    uint16_t nox_index;
    uint16_t stcc_status;
    uint16_t stcc_self_test;
    uint16_t valid;
    uint8_t rht_source;
    uint8_t gas_valid;
    uint8_t heater_stable;
    uint8_t reserved;
};

struct env_history_page {
    uint32_t request_id;
    uint64_t boot_id;
    uint32_t clock_revision;
    uint8_t flags;
};

struct env_history_status {
    uint8_t state;
    uint32_t request_id;
    struct env_history_cursor earliest;
    struct env_history_cursor latest;
    uint32_t flags;
    uint16_t page_record_count;
    uint16_t page_size;
    uint32_t page_last_sequence;
    int32_t error;
    uint64_t current_boot_id;
    uint32_t clock_revision;
    uint64_t utc_anchor_s;
    uint32_t uptime_anchor_s;
};

/* Success returns the byte count; failures return a negative errno.
 * Invalid input/capacity never changes output. All unused record values,
 * record.reserved, and unavailable RHT source are canonicalized to zero.
 */
int env_history_encode_record(const struct env_history_record *record,
                              uint8_t *output, size_t capacity);
int env_history_encode_page(const struct env_history_page *page,
                            const struct env_history_record *records, size_t count,
                            uint8_t *output, size_t capacity);
int env_history_decode_control(const uint8_t *bytes, size_t length,
                               struct env_history_control *output);
int env_history_encode_status(const struct env_history_status *status,
                              uint8_t *output, size_t capacity);
int env_history_encode_hint(uint8_t state, uint32_t request_id,
                            uint8_t *output, size_t capacity);
uint32_t env_history_crc32(const uint8_t *bytes, size_t length);

#ifdef __cplusplus
}
#endif
#endif
