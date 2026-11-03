#ifndef CARD_DISPLAY_H_
#define CARD_DISPLAY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "card_sample.h"
#include "switch_test.h"
#include <env_telemetry_ble.h>

enum card_display_mode {
	CARD_DISPLAY_GRAPH,
	CARD_DISPLAY_IMAGE,
};

/* Initialize the live dashboard; board display power must already be stable. */
int card_display_init(bool use_gas_indices);

/* Record a measurement; each ten-minute slot keeps its latest actual reading.
 * Unknown uptime axes are kept separate across boots; UTC data can be merged.
 * This performs no I/O and holds a short O(8) mutex.
 */
void card_display_record(const struct card_sample *sample);

/* Storage-worker restore API. anchor_s is the final saved UTC/uptime timestamp.
 * Stream records newest-first or oldest-first; only the newest row in each
 * ten-minute slot is kept. Unknown-UTC restore is one file/session only.
 */
void card_display_saved_begin(bool utc_axis, uint64_t anchor_s);
void card_display_saved_record(const struct card_sample *sample);
void card_display_saved_end(void);

/* Explicitly request an asynchronous refresh of the selected screen; returns
 * -EALREADY while one is queued/busy. Images may be redisplayed by this API.
 */
int card_display_refresh(void);

/* Periodic dashboard update (including the initial default graph). In image
 * mode this returns zero without queuing, loading, or refreshing the panel.
 * Image changes and user actions use the explicit APIs above/below instead.
 */
int card_display_refresh_periodic(void);

/* Select and refresh a screen. Changes during a refresh coalesce into the
 * final requested mode, without interrupting a panel update already started.
 */
int card_display_set_mode(enum card_display_mode mode);

/* Bluetooth callback: never blocks on SPI or the slow physical refresh.
 * Enrollment suppresses ordinary screens until pairing finishes/expires.
 */
void card_display_pairing_event(enum env_telemetry_pairing_event event,
                                unsigned int passkey, uint32_t request_id,
                                void *context);

/* Optional native 400x600 packed 4bpp image (EPD3IN6E_FRAME_BYTES bytes).
 * Only the panel's six colors are accepted. No image buffer is allocated.
 * Keep the buffer unchanged/alive until successfully replaced/cleared while
 * idle. Returns -EBUSY during any refresh. NULL with length zero clears it.
 * An unset image shows an explicit built-in "IMAGE NOT SET" test pattern.
 */
int card_display_set_image(const uint8_t *image, size_t length);

/* Select a persistent image prefix/file (e.g. /SD:/CARDIMG). The image service
 * verifies its header/checksum and loads it into the existing display frame.
 * No additional framebuffer or I/O occurs here. Requests made while refreshing
 * coalesce; the worker reports physical refresh success/failure to the service.
 */
int card_display_set_image_file(const char *path);

/* Boot-only registration: remember the persisted prefix without selecting an
 * image, requesting refresh, or suppressing automatic saved-history display.
 */
int card_display_register_image_file(const char *path);

/* main forwards direction presses and synthesizes PUSH only on short release:
 * PUSH graph/image, UP saved history, DOWN current graph. Long holds are owned
 * by physical BLE enrollment and never forwarded here. Releases are ignored;
 * buttons do not initiate sensor reads.
 */
void card_display_button(enum card_switch_contact contact, bool pressed,
			 uint32_t count, void *context);

#endif
