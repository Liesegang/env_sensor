#ifndef ENV_CLOCK_H_
#define ENV_CLOCK_H_

#include <stddef.h>
#include <stdint.h>
#include "env_clock_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

struct env_clock_state {
    uint8_t flags;
    uint64_t boot_id;
    uint32_t clock_revision;
    uint64_t unix_anchor_ms;
    uint64_t uptime_anchor_ms;
};

/* CT32 state and unchanged OT12 writer. Success returns 32/12; negative
 * errno on failure, without changing the output. */
int env_clock_encode_state(const struct env_clock_state *state,
                           uint8_t *output, size_t capacity);
int env_clock_decode_write(const uint8_t *bytes, size_t length, uint64_t *unix_ms);

#ifdef __cplusplus
}
#endif
#endif
