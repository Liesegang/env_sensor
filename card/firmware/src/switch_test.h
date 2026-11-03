#ifndef CARD_SWITCH_TEST_H_
#define CARD_SWITCH_TEST_H_

#include <stdbool.h>
#include <stdint.h>

enum card_switch_contact {
	CARD_SWITCH_UP,
	CARD_SWITCH_DOWN,
	CARD_SWITCH_PUSH,
};

/* Called from a workqueue after a stable press/release, never from an ISR.
 * The handler and context must remain valid until replaced by another call.
 */
typedef void (*card_switch_handler)(enum card_switch_contact contact, bool pressed,
				    uint32_t count, void *context);
void switch_test_set_handler(card_switch_handler handler, void *context);

/* Optional short observer run after each periodic poll, from its workqueue. */
void switch_test_set_poll_handler(void (*handler)(void));

/* Configure TMHU27 contacts as pulled-up GPIO inputs and start the debounced
 * test logger. Call once after board power setup; repeated calls are harmless.
 * Terminal 1 is CCW, terminal 2 is CW, and T is push. Up/down labels depend on
 * the physical orientation of S1 and must be confirmed on the assembled board.
 * P2 has no interrupt/wakeup hardware. CONFIG_CARD_SWITCH_POLL_MS > 0 enables
 * periodic reads on such pins; zero requires the caller to poll when awake.
 */
int switch_test_start(void);

/* Read all three contacts once. Safe in a thread, including a minute wake.
 * Any newly observed edge receives a separate delayed debounce check.
 */
void switch_test_poll_once(void);

#endif
