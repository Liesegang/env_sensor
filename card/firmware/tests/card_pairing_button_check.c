#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Real Card adapter with isolated hardware mocks; the shared transport and
 * its existing test mocks are not modified by these polling tests. */
#include "../src/card_ble.c"

static unsigned int repair_calls;
static int repair_error;
int env_telemetry_ble_hold_history(struct bt_conn *conn,uint32_t duration){(void)conn;(void)duration;return 0;}
int env_telemetry_ble_repair(void) { ++repair_calls; return repair_error; }
int env_telemetry_ble_init(const struct env_telemetry_ble_config *config)
{
	assert(config->set_clock_packet == set_clock_packet);
	return 0;
}
int env_telemetry_ble_publish(const struct env_telemetry_sample *sample)
{
	(void)sample;
	return 0;
}
void card_display_pairing_event(enum env_telemetry_pairing_event event,
				unsigned int passkey, uint32_t request_id, void *context)
{
	(void)event; (void)passkey; (void)request_id; (void)context;
}

static void poll_at(int64_t now, int input)
{
	unsigned int before = mock_gpio_reads;
	mock_now = now;
	mock_push_input = input;
	card_ble_check_pairing_button();
	assert(mock_now == now && !mock_sleep_calls);
	assert(mock_gpio_reads == before + 1);
}

static void reset(void)
{
	push_held_latch = false;
	push_consumed = false;
	push_started_ms = -1;
	repair_calls = 0;
	repair_error = 0;
	mock_sleep_calls = 0;
}

static void check_continuous_hold(void)
{
	reset();
	poll_at(100000, 0);
	for (int64_t now = 100000; now < 103000; now += 50) {
		poll_at(now, 1);
		assert(!repair_calls && !card_ble_pairing_button_consumed());
	}
	poll_at(102999, 1);
	assert(!repair_calls);
	poll_at(103000, 1);
	assert(repair_calls == 1 && card_ble_pairing_button_consumed());
	for (int64_t now = 103050; now <= 110000; now += 50) { poll_at(now, 1); }
	assert(repair_calls == 1); /* A held button cannot repeatedly erase bonds. */
	poll_at(110050, 0);
	assert(card_ble_pairing_button_consumed()); /* Suppress the release's UI action. */
	poll_at(110100, 1);
	assert(!card_ble_pairing_button_consumed());
	poll_at(113099, 1);
	assert(repair_calls == 1);
	poll_at(113100, 1);
	assert(repair_calls == 2 && card_ble_pairing_button_consumed());
}

static void check_interrupted_hold(int interruption)
{
	reset();
	poll_at(0, 1); /* Uptime zero is a valid initial press, not a sentinel. */
	poll_at(2950, 1);
	assert(!repair_calls);
	poll_at(2999, interruption);
	poll_at(3000, 1);
	poll_at(5999, 1);
	assert(!repair_calls);
	poll_at(6000, 1);
	assert(repair_calls == 1);
}

static void check_failed_repair(void)
{
	reset();
	repair_error = -EIO;
	poll_at(42, 1);
	poll_at(3042, 1);
	assert(repair_calls == 1 && card_ble_pairing_button_consumed());
	poll_at(100000, 1);
	assert(repair_calls == 1); /* Failure must not create an erase/retry loop. */
	poll_at(100050, -EIO);
	poll_at(100100, 1);
	assert(!card_ble_pairing_button_consumed());
	poll_at(103100, 1);
	assert(repair_calls == 2);
}

static void check_clock_sync(void)
{
	clock_state.unix_anchor_s = 0;
	clock_state.uptime_anchor_s = clock_state.revision = 0;
	assert(card_ble_unix_seconds(1000) == 0);
	uint64_t base = 1800000000123ULL;
	uint8_t packet[12] = {'O', 'T', 1, 0};
	for (size_t i = 0; i < 8; ++i) { packet[4 + i] = (uint8_t)(base >> (8 * i)); }
	mock_now = 65000;
	assert(set_clock_packet(packet, sizeof(packet), NULL) == 0);
	assert(card_ble_unix_seconds(65000) == base / 1000);
	assert(card_ble_unix_seconds(66000) == base / 1000 + 1);
	/* Convert capture time, including a sample taken before the first sync. */
	assert(card_ble_unix_seconds(64000) == base / 1000 - 1);
	assert(card_ble_unix_seconds(-1) == 0);
	assert(set_clock_packet(NULL, sizeof(packet), NULL) == -EINVAL);
	assert(set_clock_packet(packet, sizeof(packet) - 1, NULL) < 0);
	packet[2] = 2;
	assert(set_clock_packet(packet, sizeof(packet), NULL) < 0);
	packet[2] = 1;
	memset(packet + 4, 0, 8);
	assert(set_clock_packet(packet, sizeof(packet), NULL) < 0);
	memset(packet + 4, 0xff, 8);
	assert(set_clock_packet(packet, sizeof(packet), NULL) < 0);
	assert(card_ble_unix_seconds(66000) == base / 1000 + 1);
	assert(card_ble_init() == 0);
}

int main(void)
{
	check_continuous_hold();
	check_interrupted_hold(0);
	check_interrupted_hold(-EIO);
	check_interrupted_hold(2);
	check_failed_repair();
	check_clock_sync();
	puts("Card pairing button PASS: nonblocking hold, release/error reset, one request per hold");
}
