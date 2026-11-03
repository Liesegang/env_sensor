#include "switch_test.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define DIRECTION_DEBOUNCE_MS 10
#define PUSH_DEBOUNCE_MS 20
#ifndef CONFIG_CARD_SWITCH_POLL_MS
#define CONFIG_CARD_SWITCH_POLL_MS 0
#endif

/* TMHU27's code table connects 1 or 2 to common C as the wheel turns away
 * from neutral. Its separate push contact connects T to C. Card v2 grounds
 * both C terminals, so all three signals are independent active-low inputs.
 */
static const struct gpio_dt_spec contacts[] = {
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), sw1_gpios),
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), sw2_gpios),
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), swt_gpios),
};

struct contact_state {
	const char *name;
	uint32_t debounce_ms;
	const struct gpio_dt_spec *contact;
	struct gpio_callback callback;
	struct k_work_delayable debounce_work;
	struct k_spinlock lock;
	int64_t last_edge_ms;
	uint32_t edge_generation;
	bool active;
	bool observed_active;
	uint32_t activations;
};

static struct contact_state states[] = {
	{.name = "UP/CCW (1)", .debounce_ms = DIRECTION_DEBOUNCE_MS},
	{.name = "DOWN/CW (2)", .debounce_ms = DIRECTION_DEBOUNCE_MS},
	{.name = "PUSH (T)", .debounce_ms = PUSH_DEBOUNCE_MS},
};
static bool started;
static bool polling;
static struct k_work_delayable polling_work;
static struct k_spinlock handler_lock;
static card_switch_handler event_handler;
static void *event_context;
static void (*poll_handler)(void);

void switch_test_set_poll_handler(void (*handler)(void))
{
	k_spinlock_key_t key = k_spin_lock(&handler_lock);
	poll_handler = handler;
	k_spin_unlock(&handler_lock, key);
}

void switch_test_set_handler(card_switch_handler handler, void *context)
{
	k_spinlock_key_t key = k_spin_lock(&handler_lock);

	event_handler = handler;
	event_context = context;
	k_spin_unlock(&handler_lock, key);
}

static void notify_contact(struct contact_state *state, bool active, uint32_t count)
{
	k_spinlock_key_t key = k_spin_lock(&handler_lock);
	card_switch_handler handler = event_handler;
	void *context = event_context;

	k_spin_unlock(&handler_lock, key);
	if (handler != NULL) {
		handler((enum card_switch_contact)(state - states), active, count, context);
	}
}

static void debounce_contact(struct k_work *work)
{
	struct contact_state *state =
		CONTAINER_OF(k_work_delayable_from_work(work), struct contact_state, debounce_work);
	k_spinlock_key_t key = k_spin_lock(&state->lock);
	uint32_t generation = state->edge_generation;
	int64_t edge_ms = state->last_edge_ms;

	k_spin_unlock(&state->lock, key);
	int64_t elapsed = k_uptime_get() - edge_ms;

	if (elapsed < state->debounce_ms) {
		k_work_reschedule(&state->debounce_work, K_MSEC(state->debounce_ms - elapsed));
		return;
	}
	int raw = gpio_pin_get_dt(state->contact);

	if (raw < 0) {
		printk("SW read failed: %s error=%d\n", state->name, raw);
		return;
	}
	bool active = raw != 0;
	bool changed = false;
	uint32_t count;

	key = k_spin_lock(&state->lock);
	/* If an edge arrived during the read, defer until that new edge is stable. */
	if (generation != state->edge_generation) {
		elapsed = k_uptime_get() - state->last_edge_ms;
		k_spin_unlock(&state->lock, key);
		int64_t remaining = (int64_t)state->debounce_ms - elapsed;

		k_work_reschedule(&state->debounce_work, K_MSEC(remaining > 0 ? remaining : 1));
		return;
	}
	if (active != state->active) {
		state->active = active;
		changed = true;
		if (active) {
			++state->activations;
		}
	}
	count = state->activations;
	k_spin_unlock(&state->lock, key);
	if (changed) {
		printk("SW %s %s count=%u\n", state->name,
		       active ? "pressed" : "released", count);
		notify_contact(state, active, count);
	}
}

void switch_test_poll_once(void)
{
	if (!started || !polling) {
		return;
	}
	for (size_t i = 0; i < ARRAY_SIZE(states); ++i) {
		struct contact_state *state = &states[i];
		int raw = gpio_pin_get_dt(state->contact);

		if (raw < 0) {
			printk("SW read failed: %s error=%d\n", state->name, raw);
			continue;
		}
		bool changed = false;
		bool active = raw != 0;
		k_spinlock_key_t key = k_spin_lock(&state->lock);

		if (active != state->observed_active) {
			state->observed_active = active;
			state->last_edge_ms = k_uptime_get();
			++state->edge_generation;
			changed = true;
		}
		k_spin_unlock(&state->lock, key);
		if (changed) {
			k_work_reschedule(&state->debounce_work, K_MSEC(state->debounce_ms));
		}
	}
}

static void poll_contacts(struct k_work *work)
{
	ARG_UNUSED(work);
	switch_test_poll_once();
	k_spinlock_key_t key = k_spin_lock(&handler_lock);
	void (*observer)(void) = poll_handler;
	k_spin_unlock(&handler_lock, key);
	if (observer) { observer(); }
	if (CONFIG_CARD_SWITCH_POLL_MS > 0) {
		k_work_reschedule(&polling_work, K_MSEC(CONFIG_CARD_SWITCH_POLL_MS));
	}
}

static void contact_edge(const struct device *port, struct gpio_callback *callback,
			 gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	struct contact_state *state = CONTAINER_OF(callback, struct contact_state, callback);
	k_spinlock_key_t key = k_spin_lock(&state->lock);

	state->last_edge_ms = k_uptime_get();
	++state->edge_generation;
	k_spin_unlock(&state->lock, key);
	/* Each edge restarts this contact's timer. Idle switches cause no wakeups. */
	k_work_reschedule(&state->debounce_work, K_MSEC(state->debounce_ms));
}

int switch_test_start(void)
{
	if (started) {
		return 0;
	}
	for (size_t i = 0; i < ARRAY_SIZE(contacts); ++i) {
		if (!gpio_is_ready_dt(&contacts[i])) {
			return -ENODEV;
		}
		int err = gpio_pin_configure_dt(&contacts[i], GPIO_INPUT | GPIO_PULL_UP);

		if (err != 0) {
			return err;
		}
		states[i].contact = &contacts[i];
		states[i].last_edge_ms = k_uptime_get();
		k_work_init_delayable(&states[i].debounce_work, debounce_contact);
		gpio_init_callback(&states[i].callback, contact_edge, BIT(contacts[i].pin));
	}
	size_t callbacks_added = 0;
	int err = 0;

	for (size_t i = 0; i < ARRAY_SIZE(contacts); ++i) {
		err = gpio_add_callback(contacts[i].port, &states[i].callback);
		if (err != 0) {
			break;
		}
		++callbacks_added;
		err = gpio_pin_interrupt_configure_dt(&contacts[i], GPIO_INT_EDGE_BOTH);
		if (err != 0) {
			break;
		}
	}
	if (err != 0) {
		for (size_t i = 0; i < callbacks_added; ++i) {
			gpio_pin_interrupt_configure_dt(&contacts[i], GPIO_INT_DISABLE);
			gpio_remove_callback(contacts[i].port, &states[i].callback);
		}
		for (size_t i = 0; i < callbacks_added; ++i) {
			struct k_work_sync sync;

			k_work_cancel_delayable_sync(&states[i].debounce_work, &sync);
		}
		if (err != -ENOTSUP) {
			return err;
		}
		/* GPIO P2 on nRF54L15 has neither GPIOTE nor SENSE/DETECT. */
		polling = true;
	}
	started = true;
	for (size_t i = 0; i < ARRAY_SIZE(contacts); ++i) {
		/* Also report a contact already held at startup after its debounce time. */
		k_work_reschedule(&states[i].debounce_work, K_MSEC(states[i].debounce_ms));
	}
	if (polling) {
		k_work_init_delayable(&polling_work, poll_contacts);
		if (CONFIG_CARD_SWITCH_POLL_MS > 0) {
			k_work_reschedule(&polling_work, K_MSEC(CONFIG_CARD_SWITCH_POLL_MS));
			printk("SW: P2 cannot interrupt/wake; periodic polling %d ms\n",
			       CONFIG_CARD_SWITCH_POLL_MS);
		} else {
			printk("SW: P2 cannot interrupt/wake; caller must poll while awake\n");
		}
	}
	printk("SW READY TMHU27 P2.01=UP/CCW P2.02=DOWN/CW P2.05=PUSH "
	       "debounce=%d/%dms %s\n", DIRECTION_DEBOUNCE_MS, PUSH_DEBOUNCE_MS,
	       polling ? "polled" : "interrupt-driven");
	return 0;
}
