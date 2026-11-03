/* SPDX-License-Identifier: MIT */
#ifndef CARD_BLE_H_
#define CARD_BLE_H_
#include "card_sample.h"
#include <opt4001.h>
#include <bme690.h>
#include <sgp41.h>
#include <stcc4.h>
#include <sfa40.h>

int card_ble_init(void);
/* Immediate, nonblocking check called by the 50-ms switch polling observer.
 * A continuous three-second PUSH hold authorizes replacing the owner's bond.
 */
void card_ble_check_pairing_button(void);
bool card_ble_pairing_button_consumed(void);
/* UTC seconds for a captured MCU uptime; zero until authenticated OT sync. */
uint64_t card_ble_unix_seconds(int64_t uptime_ms);
struct card_ble_clock {
    uint64_t boot_id;
    uint64_t unix_anchor_s;
    uint32_t uptime_anchor_s;
    uint32_t revision;
};
/* One coherent snapshot for history status/pages. UTC anchor/revision are zero
 * before an authenticated receiver sync; identity is unique for this boot.
 */
void card_ble_clock_snapshot(struct card_ble_clock *clock);
/* Same-sample snapshots; NULL means missing. Data is copied before return.
 * SFA40 is configured as enabled; an absent sensor sends an error TLV.
 * SGP41 index-valid flags are preserved; a raw burst does not imply indices.
 */
int card_ble_publish(const struct card_sample *sample,
                     const struct opt4001_sample *opt4001,
                     const struct bme690_sample *bme690,
                     const struct sgp41_sample *sgp41,
                     const struct stcc4_sample *stcc4,
                     const struct sfa40_sample *sfa40);
#endif /* CARD_BLE_H_ */
