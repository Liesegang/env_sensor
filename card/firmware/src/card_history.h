#ifndef CARD_HISTORY_H_
#define CARD_HISTORY_H_

/* Append-only authenticated GATT history service. Its worker reads SD and the
 * existing pending measurement ring; no filesystem call runs in BLE callbacks.
 */
int card_history_init(void);

#endif
