/* SPDX-License-Identifier: MIT */
#ifndef EPD_DASHBOARD_H_
#define EPD_DASHBOARD_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPD_DASHBOARD_SAMPLES 144U
#define EPD_DASHBOARD_SLOT_SECONDS 600U
#define EPD_DASHBOARD_SERIES 8U
#define EPD_DASHBOARD_VALIDITY_BYTES ((EPD_DASHBOARD_SAMPLES + 7U) / 8U)

struct epd_dashboard_series {
    const char *name; /* 1..8 characters, null terminated. */
    const char *unit; /* 0..6 characters, null terminated. */
    uint8_t lineColor;
    uint8_t mixColor; /* Same color for solid lines; a second color for mixing. */
    float plotLow;
    float plotHigh;
    float veryLowHigh;
    float lowHigh;
    float midHigh;
    float highHigh;
    /* Optional exact current value; cards need not clip to the graph range. */
    float current_value;
    uint8_t has_current_value;
    uint8_t decimals; /* 0, 1 or 2 decimal places. */
    /* Oldest to newest, ten minutes per slot, normalized to 0..65535. */
    uint16_t values[EPD_DASHBOARD_SAMPLES];
    /* Zero preserves legacy behavior: all samples are present. */
    uint8_t has_validity_map;
    /* When enabled, bit n (LSB first) marks values[n] as a real sample. */
    uint8_t valid_bits[EPD_DASHBOARD_VALIDITY_BYTES];
};

/*
 * Render the 600x400 landscape dashboard into the native 400x600, 4bpp frame.
 * length must equal EPD3IN6E_FRAME_BYTES; count must equal 8.
 * Invalid input returns -EINVAL before modifying frame. No allocation or I/O.
 * Samples map linearly from 0..65535 to each series' plotLow..plotHigh.
 * Colors must be an epd3in6e color; ranges/thresholds must be finite and ordered.
 * With has_validity_map set, missing samples stay blank and gaps are not joined.
 * A missing latest slot shows "--" and "NA" in that channel's card.
 * has_current_value uses a finite exact current_value for a present latest card;
 * the graph still uses values[]. Zero preserves normalized latest card behavior.
 * Right-align a partial history within all 144 time slots; left slots invalid.
 * Caller must keep buffers and strings alive for the synchronous call.
 */
int epd_dashboard_render(uint8_t *frame, size_t length,
                         const struct epd_dashboard_series *series, size_t count);

/* Same dashboard, with an explicit time-basis caption (<=85 ASCII characters)
 * and right-edge label (<=4 characters). NULL preserves the original labels. */
int epd_dashboard_render_labeled(uint8_t *frame, size_t length,
                         const struct epd_dashboard_series *series, size_t count,
                         const char *caption, const char *right_label);

/*
 * Render deterministic synthetic 24-hour history in the prototype's layout.
 * Includes HCHO as synthetic data, regardless of installed sensors.
 * Uses 8 static series (~3 KiB); serialize demo calls.
 */
int epd_dashboard_demo_render(uint8_t *frame, size_t length);

/* Fresh six-digit BLE passkey screen, ready before starting advertising.
 * Returns -EINVAL before modifying frame if passkey > 999999 or size differs.
 */
int epd_dashboard_pairing_render(uint8_t *frame, size_t length, uint32_t passkey);

#ifdef __cplusplus
}
#endif
#endif /* EPD_DASHBOARD_H_ */
