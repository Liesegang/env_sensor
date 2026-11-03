#ifndef CARD_SD_LOGGER_H_
#define CARD_SD_LOGGER_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct card_sample;

/* Start the storage worker and restore saved display history asynchronously.
 * The display must have been initialized first. Neither init nor queue does I/O.
 * A 640-row ring holds a 600-second batch plus concurrent-write headroom.
 * A full ring rejects new rows with -ENOSPC; committed rows are never overwritten.
 */
int sd_logger_init(void);
int sd_logger_queue(const struct card_sample *sample);
int sd_logger_flush_async(void);

/* Called by the display when this boot first obtains UTC. Re-scan only UTC
 * sessions, skipping newer clock-unknown files, then merge the restored data.
 */
int sd_logger_restore_utc_async(void);

/* Assign the current boot's nonzero 63-bit identity before starting storage.
 * It is written as CSV metadata without enlarging the 72-byte sample ring.
 */
int sd_logger_set_boot_id(uint64_t boot_id);

struct sd_logger_cursor { uint64_t boot_id; uint32_t sequence; };
struct sd_logger_history_page {
    struct sd_logger_cursor earliest, latest;
    uint64_t boot_id;
    size_t count;
    bool more, gap;
};
/* Worker-only history page, in chronological file/sequence order. A zero
 * cursor starts at the oldest retained row; a missing cursor explicitly sets
 * gap and restarts there. The selected page never spans MCU boot identities.
 * SD I/O and pending RAM are serialized with the existing storage worker;
 * sending or acknowledging history never removes measured SD/RAM records.
 */
int sd_logger_history_read(struct sd_logger_cursor after,
                          struct card_sample *samples, size_t capacity,
                          struct sd_logger_history_page *page);
/* RAM-only reconnect notification. The next worker read rebuilds metadata;
 * it does not wait for the SD/EPD lock or perform I/O in a BLE callback.
 */
void sd_logger_history_reset(void);

/* Serialize the board's SD/EPD transactions on their shared SPI controller.
 * These sleepable locks are only for their I/O workers, never the sensor loop.
 */
void sd_logger_bus_lock(void);
void sd_logger_bus_unlock(void);

/* File-backed image I/O: begin owns the shared bus and a fresh FAT mount.
 * On success every path must call end, even after an FS/CRC failure.
 * Do not combine these with bus_lock or call them from a BLE callback.
 */
int sd_logger_storage_begin(void);
int sd_logger_storage_end(void);

/* Compatibility synchronous append; new main code should use the worker APIs.
 * Append a batch to a new, unused LOGxxxxx.CSV for this MCU boot.
 * CONFIG_CARD_TEST_CADENCE selects TSTxxxxx.CSV and verifies the header and
 * complete batch by reading after close/unmount/remount.
 * unix_s is optional receiver-synchronized UTC; zero means unknown. Missing readings are
 * empty CSV cells. The SD card must already contain a FAT filesystem.
 *
 * Always remove the first *committed samples from the caller's pending batch,
 * including when this returns an error from close/unmount/readback after a successful
 * sync. Keep all remaining samples for retry. Failed writes are rolled back
 * before another batch can be appended; recovery state lasts until MCU reset.
 * Its bus lock serializes it with the display. It must not run in BLE callbacks
 * or the 1Hz measurement thread.
 */
int sd_logger_append(const struct card_sample *samples, size_t count, size_t *committed);

#endif
