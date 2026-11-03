/* SPDX-License-Identifier: MIT */
#include <env_telemetry_ble.h>

#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
#include <zephyr/settings/settings.h>
#endif

static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(
	ENV_TELEMETRY_SERVICE_UUID_BYTES);
static struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(
	ENV_TELEMETRY_CHARACTERISTIC_UUID_BYTES);
static uint8_t latest[ENV_TELEMETRY_MAX_SIZE];
static size_t latest_length;
/* Freeze offset-based ATT long reads while newer notifications are published. */
static uint8_t read_frame[ENV_TELEMETRY_MAX_SIZE];
static size_t read_length;
static struct bt_conn *connection;
static bool started;
static bool stack_enabled;
static bool stack_faulted;
/* Expiry owns a short HCI disconnect; lifecycle may not close HCI yet. */
static bool expiry_disconnect_pending;
/* bt_disable clears host identities; restore this unbonded peripheral's ID. */
#if !defined(CONFIG_ENV_TELEMETRY_SECURE)
static bt_addr_le_t identity;
static bool identity_saved;
#endif
static uint32_t frame_sequence(const uint8_t *frame)
{
    return (uint32_t)frame[12] | (uint32_t)frame[13] << 8 |
           (uint32_t)frame[14] << 16 | (uint32_t)frame[15] << 24;
}
static struct env_telemetry_ble_config configuration;
/* Protected by state_lock; zero is paused in windowed mode. */
static int64_t active_until_ms;
static int64_t transfer_hold_until_ms;
static int64_t history_hold_until_ms;
static int64_t notify_hold_until_ms;
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
static int64_t enrollment_until_ms;
static bool repair_pending;
static bool discard_bond_pending;
static bool verify_bond_pending;
static bool new_pin_pending;
static bool pin_display_pending;
static bool pin_available;
static bool pairing_attempt;
static uint32_t prepared_pin;
static uint32_t pin_request_id;
#if defined(CONFIG_BT_GATT_CACHING)
static bool gatt_boot_settled;
#endif
#define DATA_PERMISSIONS (BT_GATT_PERM_READ_LESC | BT_GATT_PERM_WRITE_LESC | \
                         BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN)
#else
#define DATA_PERMISSIONS (BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
#endif
BUILD_ASSERT(CONFIG_BT_MAX_CONN == 1, "Shared telemetry BLE supports one connection");
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
BUILD_ASSERT(CONFIG_BT_MAX_PAIRED == 1 && CONFIG_BT_ID_MAX == 1,
             "Secure telemetry supports one persistent owner and one identity");
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_PRIVACY),
             "Secure telemetry persists one static identity; privacy is not supported");
#endif
K_MUTEX_DEFINE(state_lock);
K_SEM_DEFINE(updated, 0, 1);
K_SEM_DEFINE(connection_recycled, 0, 1);

/* Caller holds state_lock. Timers always recheck the current deadline. */
static bool radio_active_locked(void)
{
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
    if (repair_pending || discard_bond_pending || verify_bond_pending ||
        new_pin_pending || pin_display_pending) { return false; }
#endif
    return started && stack_enabled && !stack_faulted && !expiry_disconnect_pending &&
                     (!configuration.active_window_ms ||
                      active_until_ms > k_uptime_get());
}

static ssize_t read_snapshot(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buffer, uint16_t len, uint16_t offset)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	if (!radio_active_locked()) {
		k_mutex_unlock(&state_lock);
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	if (offset == 0) {
		read_length = latest_length;
		memcpy(read_frame, latest, read_length);
	}
	ssize_t result = read_length == 0 ? BT_GATT_ERR(BT_ATT_ERR_UNLIKELY) :
		bt_gatt_attr_read(conn, attr, buffer, len, offset, read_frame, read_length);
	k_mutex_unlock(&state_lock);
	return result;
}

static void subscription_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	printk("BLE notifications %s\n", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
	if (value == BT_GATT_CCC_NOTIFY) {
		k_sem_give(&updated);
	}
}

static ssize_t write_clock(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buffer, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn); ARG_UNUSED(attr);
	if (offset || (flags & BT_GATT_WRITE_FLAG_PREPARE)) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
	if (len != 12) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN); }
	const uint8_t *packet = buffer;
	if (packet[0] != 'O' || packet[1] != 'T' || packet[2] != 1 || packet[3] != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	uint64_t unix_ms = 0;
	for (size_t i = 0; i < 8; ++i) { unix_ms |= (uint64_t)packet[4 + i] << (8 * i); }
	if (unix_ms < 1577836800000ULL || unix_ms >= 4102444800000ULL) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (!configuration.set_clock_packet) { return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED); }
	int err = configuration.set_clock_packet(buffer, len, configuration.context);
	if (err) { return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED); }
	printk("CLOCK: synchronized from BLE receiver\n");
	return len;
}

BT_GATT_SERVICE_DEFINE(environment_service,
	BT_GATT_PRIMARY_SERVICE(&service_uuid),
	BT_GATT_CHARACTERISTIC(&data_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
			       DATA_PERMISSIONS, read_snapshot, write_clock, NULL),
	BT_GATT_CCC(subscription_changed, DATA_PERMISSIONS));

static const struct bt_data advertisement[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		ENV_TELEMETRY_SERVICE_UUID_BYTES),
};
static const struct bt_data scan_response[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void advertise(struct k_work *work);
static void expire_window(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(advertising_work, advertise);
K_WORK_DELAYABLE_DEFINE(window_expiry_work, expire_window);

#if defined(CONFIG_ENV_TELEMETRY_SECURE)
/* Application callback is deliberately outside state_lock and must never wait
 * for the panel. The completed physical refresh acknowledges its request ID. */
static void pairing_event(enum env_telemetry_pairing_event event,
                          unsigned int pin, uint32_t request_id)
{
	configuration.pairing_event(event, pin, request_id, configuration.context);
}

static void count_bond(const struct bt_bond_info *info, void *context)
{
	ARG_UNUSED(info);
	++*(unsigned int *)context;
}

static void settings_barrier_handler(struct k_work *work) { ARG_UNUSED(work); }
K_WORK_DEFINE(settings_barrier, settings_barrier_handler);

struct identity_check { bt_addr_le_t address; bool seen; bool found; int error; };
static int check_stored_identity(const char *name, size_t length,
                                settings_read_cb read, void *argument, void *context)
{
	struct identity_check *check = context;
	if (name && name[0]) { return 0; }
	check->seen = true;
	bt_addr_le_t saved;
	if (length != sizeof(saved)) {
		printk("ERROR: BLE SECURITY stored ID length=%u expected=%u\n", (unsigned int)length, (unsigned int)sizeof(saved));
		check->error = -EIO; return check->error;
	}
	ssize_t received = read(argument, &saved, sizeof(saved));
	if (received != sizeof(saved)) {
		printk("ERROR: BLE SECURITY stored ID read=%d expected=%u\n", (int)received, (unsigned int)sizeof(saved));
		check->error = -EIO; return check->error;
	}
	if (memcmp(&saved, &check->address, sizeof(saved))) {
		printk("ERROR: BLE SECURITY stored ID differs from running identity\n");
		check->error = -EIO; return check->error;
	}
	check->found = true;
	return 0;
}

static int count_stored_key(const char *name, size_t length,
                            settings_read_cb read, void *argument, void *context)
{
	ARG_UNUSED(name); ARG_UNUSED(read); ARG_UNUSED(argument);
	if (length) { ++*(unsigned int *)context; }
	return 0;
}

static int verify_stored_key_count(unsigned int expected)
{
	unsigned int count = 0;
	int err = settings_load_subtree_direct("bt/keys", count_stored_key, &count);
	return err ? err : count == expected ? 0 : -EIO;
}

static int load_bluetooth_settings(void)
{
	int err = settings_load_subtree("bt");
	printk("BLE SECURITY: settings load result=%d\n", err);
	if (!err) {
		/* NCS 3.4.1 queues database-hash/Service Changed work with a 10-ms
		 * timeout. At first boot after an update, let it become runnable
		 * before the FIFO barrier below. Otherwise initial bt_disable can
		 * clear BT_DEV_READY/keys before the offline changed range is saved.
		 * Later minute restarts use the already initialized static database. */
#if defined(CONFIG_BT_GATT_CACHING)
		if (!gatt_boot_settled) { k_msleep(20); }
#endif
		/* SDK commit submits store_id_work, whose handler reads id_count at
		 * execution time. Finish that FIFO work before bt_disable clears it.
		 * A private noop barrier uses the public kernel flush API; it does
		 * not drain/plug the shared queue or access a private SDK work item. */
		struct k_work_sync sync;
		int submitted = k_work_submit(&settings_barrier);
		if (submitted < 0) { err = submitted; }
		else { (void)k_work_flush(&settings_barrier, &sync); }
#if defined(CONFIG_BT_GATT_CACHING)
		if (!err) { gatt_boot_settled = true; }
#endif
		printk("BLE SECURITY: identity storage barrier submit=%d result=%d\n", submitted, err);
	}
	if (!err) {
		struct identity_check check = {0};
		size_t count = 1;
		bt_id_get(&check.address, &count);
		printk("BLE SECURITY: running identity count=%u\n", (unsigned int)count);
		if (count != 1) { err = -EIO; }
		else {
			err = settings_load_subtree_direct("bt/id", check_stored_identity, &check);
			if (!err) { err = check.error; }
			if (!err && !check.seen) {
				/* With privacy disabled, the SDK can obtain a stable factory
				 * controller address without setting BT_DEV_STORE_ID. Persist
				 * that first identity explicitly using the SDK's bt/id format.
				 * Never replace an existing malformed or different identity. */
				err = settings_save_one("bt/id", &check.address, sizeof(check.address));
				printk("BLE SECURITY: first identity persistence result=%d\n", err);
				if (!err) {
					check.seen = false; check.found = false;
					err = settings_load_subtree_direct("bt/id", check_stored_identity, &check);
					if (!err) { err = check.error; }
				}
			}
			printk("BLE SECURITY: stored identity direct result=%d seen=%u matched=%u\n", err, check.seen, check.found);
			if (!err && !check.found) { err = -EIO; }
		}
	}
	unsigned int bonds = 0;
	if (!err) { bt_foreach_bond(BT_ID_DEFAULT, count_bond, &bonds); }
	if (!err) { printk("BLE SECURITY: identity/keys loaded; owners=%u\n", bonds); }
	return err;
}

static int enable_with_settings(void)
{
	int err = bt_enable(NULL);
	printk("BLE SECURITY: bt_enable result=%d\n", err);
	if (!err) {
		err = load_bluetooth_settings();
		if (err) {
			/* An unreadable key store must not leave the controller running
			 * after the lifecycle enters its fail-closed state. */
			int close_error = bt_disable();
			if (close_error) { printk("BLE SECURITY: failed settings shutdown error=%d\n", close_error); }
		}
	}
	return err;
}

static enum bt_security_err pairing_accept(struct bt_conn *conn,
                         const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(conn); ARG_UNUSED(feat);
	k_mutex_lock(&state_lock, K_FOREVER);
	bool allowed = radio_active_locked() && enrollment_until_ms > k_uptime_get() &&
		pin_available && !pairing_attempt;
	if (allowed) { pairing_attempt = true; }
	k_mutex_unlock(&state_lock);
	if (!allowed) { printk("BLE SECURITY: new pairing rejected; physical enrollment required\n"); }
	return allowed ? BT_SECURITY_ERR_SUCCESS : BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}

static uint32_t app_passkey(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	k_mutex_lock(&state_lock, K_FOREVER);
	/* pairing_accept admits exactly one attempt for each displayed PIN. */
	uint32_t pin = prepared_pin;
	pin_available = false;
	k_mutex_unlock(&state_lock);
	return pin;
}

static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	ARG_UNUSED(conn);
	/* Already on the physical panel before advertising started. */
	k_mutex_lock(&state_lock, K_FOREVER);
	bool matches = pairing_attempt && passkey == prepared_pin;
	k_mutex_unlock(&state_lock);
	if (!matches) { (void)bt_conn_auth_cancel(conn); }
	printk("BLE SECURITY: displayed one-use passkey; enter it on the iPhone\n");
}

static void invalidate_attempt(void)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	pin_available = false;
	pairing_attempt = false;
	if (enrollment_until_ms > k_uptime_get()) {
		/* Stop this link before preparing a different PIN. Independent expiry
		 * disconnect also unblocks a sender waiting on ATT buffers. */
		new_pin_pending = true;
		pin_display_pending = false;
		active_until_ms = k_uptime_get();
		k_work_reschedule(&window_expiry_work, K_NO_WAIT);
	}
	k_mutex_unlock(&state_lock);
	k_sem_give(&updated);
}

static void pairing_cancel(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	invalidate_attempt();
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err error)
{
	ARG_UNUSED(conn);
	printk("BLE SECURITY: pairing failed error=%u; preparing a fresh PIN\n", error);
	invalidate_attempt();
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	if (!bonded || bt_conn_get_security(conn) != BT_SECURITY_L4) {
		invalidate_attempt();
		return;
	}
	k_mutex_lock(&state_lock, K_FOREVER);
	bool authorized = pairing_attempt && enrollment_until_ms > k_uptime_get() &&
	                  !repair_pending && !discard_bond_pending && radio_active_locked();
	if (!authorized) {
		/* SDK saves the new key before this callback. A completion queued
		 * across expiry/physical reset may not reopen that closed attempt.
		 * Delete its persisted bond from the ordinary sender thread. */
		discard_bond_pending = true;
		pin_available = false;
		pairing_attempt = false;
		active_until_ms = k_uptime_get();
		k_work_reschedule(&window_expiry_work, K_NO_WAIT);
		k_mutex_unlock(&state_lock);
		printk("BLE SECURITY: late pairing completion rejected; saved bond removal queued\n");
		k_sem_give(&updated);
		return;
	}
	pin_available = false;
	pairing_attempt = false;
	verify_bond_pending = true;
	k_mutex_unlock(&state_lock);
	/* SDK's smp.c ignores bt_keys_store errors. Verify persistent storage
	 * from the sender thread before declaring a durable owner. */
	printk("BLE SECURITY: authenticated SC; verifying saved owner bond\n");
	k_sem_give(&updated);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                              enum bt_security_err error)
{
	ARG_UNUSED(conn);
	printk("BLE SECURITY: level=%u error=%u\n", level, error);
	if (!error && level == BT_SECURITY_L4) { k_sem_give(&updated); }
}

static const struct bt_conn_auth_cb authentication = {
	.pairing_accept = pairing_accept,
	.app_passkey = app_passkey,
	.passkey_display = passkey_display,
	.cancel = pairing_cancel,
};
static struct bt_conn_auth_info_cb authentication_info = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};
#endif

static void expire_window(struct k_work *work)
{
	ARG_UNUSED(work);
	k_mutex_lock(&state_lock, K_FOREVER);
	if (!started || !configuration.active_window_ms || !active_until_ms) {
		k_mutex_unlock(&state_lock);
		return;
	}
	int64_t remaining = active_until_ms - k_uptime_get();
	if (remaining > 0) {
		/* A new publication may have extended the previous timer's window. */
		k_work_reschedule(&window_expiry_work, K_MSEC(remaining));
		k_mutex_unlock(&state_lock);
		return;
	}
	active_until_ms = 0;
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	bool close_enrollment = enrollment_until_ms && enrollment_until_ms <= k_uptime_get();
	if (close_enrollment) {
		enrollment_until_ms = 0;
		pin_available = false;
		new_pin_pending = false;
		pin_display_pending = false;
		pairing_attempt = false;
	}
#endif
	/* Cancel under the same lock used by publish scheduling; a new window's
	 * advertise cannot be accidentally cancelled after this unlock. */
	k_work_cancel_delayable(&advertising_work);
	if (configuration.disable_between_windows) {
		/* HCI reset/connection cleanup can wait on other work queues. The
		 * normal thread performs disable after finishing its current notify. */
		/* If lifecycle already took teardown ownership, no new HCI request
		 * may race its controller close. Otherwise reserve this request. */
		struct bt_conn *conn = stack_enabled && connection ? bt_conn_ref(connection) : NULL;
		expiry_disconnect_pending = conn != NULL;
		k_mutex_unlock(&state_lock);
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
		if (close_enrollment) {
			pairing_event(ENV_TELEMETRY_PAIRING_CLOSED, 0, pin_request_id);
			printk("BLE SECURITY: enrollment expired; PUSH hold required to retry\n");
		}
#endif
		if (conn) {
			/* The HCI disconnect command uses its own buffers and only waits
			 * for controller acceptance, not the peer/disconnected callback.
			 * Disconnect cleanup releases ATT buffers, waking a sender that
			 * was blocked in bt_gatt_notify at a small negotiated MTU. */
			int err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			if (err && err != -ENOTCONN) { printk("BLE: window disconnect error=%d\n", err); }
			bt_conn_unref(conn);
		}
		k_mutex_lock(&state_lock, K_FOREVER);
		expiry_disconnect_pending = false;
		k_mutex_unlock(&state_lock);
		k_sem_give(&updated);
		printk("BLE: window closed duration_ms=%u; stack shutdown requested\n",
		       configuration.active_window_ms);
		return;
	}
	struct bt_conn *conn = connection ? bt_conn_ref(connection) : NULL;
	k_mutex_unlock(&state_lock);
	/* Start/stop both run on the system queue, so a new advertise cannot race
	 * this stop. Publication schedules the next start; recycled gates it again. */
	int err = bt_le_adv_stop();
	if (err && err != -EALREADY) { printk("BLE: window stop advertising error=%d\n", err); }
	if (conn) {
		err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err && err != -ENOTCONN) { printk("BLE: window disconnect error=%d\n", err); }
		bt_conn_unref(conn);
	}
	printk("BLE: window closed duration_ms=%u; radio paused\n", configuration.active_window_ms);
}

static void start_advertising(void)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	bool connected = connection != NULL;
	bool ready = radio_active_locked();
	k_mutex_unlock(&state_lock);
	if (connected || !ready) {
		return;
	}
	const struct bt_le_adv_param parameters = {
		.id = BT_ID_DEFAULT, .options = BT_LE_ADV_OPT_CONN,
		.interval_min = configuration.advertising_interval_min,
		.interval_max = configuration.advertising_interval_max,
	};
	int err = bt_le_adv_start(&parameters, advertisement, ARRAY_SIZE(advertisement),
				 scan_response, ARRAY_SIZE(scan_response));
	if (err != 0 && err != -EALREADY) {
		printk("ERROR: BLE advertise: %d\n", err);
		k_work_reschedule(&advertising_work, K_MSEC(500));
	} else {
		printk("BLE advertising: %s service=%s\n", CONFIG_BT_DEVICE_NAME, ENV_TELEMETRY_SERVICE_UUID);
	}
}

static void advertise(struct k_work *work)
{
	ARG_UNUSED(work);
	if (configuration.disable_between_windows) {
		/* Serialize all controller start/stop calls on the sender thread. */
		k_sem_give(&updated);
		return;
	}
	start_advertising();
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		printk("ERROR: BLE connection: %u\n", err);
		return;
	}
	k_mutex_lock(&state_lock, K_FOREVER);
	/* A queued connection event may arrive after teardown took ownership.
	 * The stack will clean that event; do not add another application ref. */
	if (!configuration.disable_between_windows || stack_enabled) {
		connection = bt_conn_ref(conn);
	}
	bool active = radio_active_locked();
	k_mutex_unlock(&state_lock);
	printk("BLE connected\n");
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	if (active) {
		int security_error = bt_conn_set_security(conn, BT_SECURITY_L4);
		if (security_error) { printk("BLE SECURITY: request L4 error=%d\n", security_error); }
	}
#endif
	if (!active) {
		if (configuration.disable_between_windows) {
			/* Lifecycle handles late arrivals; no RX callback can issue HCI
			 * after the sender took controller teardown ownership. */
			k_sem_give(&updated);
		} else {
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	if (connection == conn) {
		transfer_hold_until_ms = 0;
		history_hold_until_ms = 0;
		notify_hold_until_ms = 0;
		if (configuration.active_window_ms && active_until_ms) {
			active_until_ms = k_uptime_get() + configuration.active_window_ms;
			k_work_reschedule(&window_expiry_work, K_MSEC(configuration.active_window_ms));
		}
		bt_conn_unref(connection);
		connection = NULL;
	}
	k_mutex_unlock(&state_lock);
	printk("BLE disconnected: %u\n", reason);
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	/* A dropped attempt must never reuse the PIN on a different connection. */
	k_mutex_lock(&state_lock, K_FOREVER);
	bool retry = pairing_attempt;
	k_mutex_unlock(&state_lock);
	if (retry) { invalidate_attempt(); }
#endif
}

static void recycled(void)
{
	/* The slot must be free and the current RF window must still be open. */
	k_sem_give(&connection_recycled);
	k_mutex_lock(&state_lock, K_FOREVER);
	bool active = radio_active_locked();
	if (active) { k_work_reschedule(&advertising_work, K_NO_WAIT); }
	k_mutex_unlock(&state_lock);
}

static void le_parameters_updated(struct bt_conn *conn, uint16_t interval,
				 uint16_t latency, uint16_t timeout)
{
	ARG_UNUSED(conn);
	printk("BLE parameters: interval_units=%u latency=%u timeout_units=%u\n",
	       interval, latency, timeout);
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.le_param_updated = le_parameters_updated,
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	.security_changed = security_changed,
#endif
};

static void count_connection(struct bt_conn *conn, void *context)
{
	ARG_UNUSED(conn);
	++*(unsigned int *)context;
}

static int wait_for_connection_cleanup(void)
{
	int64_t deadline = k_uptime_get() + 1000;
	for (;;) {
		unsigned int count = 0;
		bt_conn_foreach(BT_CONN_TYPE_ALL, count_connection, &count);
		if (!count) { return 0; }
		/* disconnected/deferred/recycled callbacks run on the system queue;
		 * wait here without holding state_lock or blocking that queue. */
		int64_t remaining = deadline - k_uptime_get();
		if (remaining <= 0 || k_sem_take(&connection_recycled, K_MSEC(remaining))) { return -EBUSY; }
	}
}

/* Called only from the normal sender thread, before acquiring a notify ref.
 * No BT lifecycle API runs on the system workqueue or under state_lock. */
static void update_stack_lifecycle(void)
{
	if (!configuration.disable_between_windows) { return; }
	k_mutex_lock(&state_lock, K_FOREVER);
	if (expiry_disconnect_pending) {
		k_mutex_unlock(&state_lock);
		return; /* Expiry will give updated again after HCI accepts disconnect. */
	}
	bool faulted = stack_faulted;
	bool wanted = started && active_until_ms > k_uptime_get();
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	wanted = wanted || (started && (repair_pending || discard_bond_pending ||
	                               verify_bond_pending || new_pin_pending));
#endif
	bool enabled = stack_enabled;
	struct bt_conn *closing_conn = NULL;
	if (!faulted && !wanted && enabled) {
		stack_enabled = false;
		active_until_ms = 0;
		k_work_cancel_delayable(&window_expiry_work);
		k_work_cancel_delayable(&advertising_work);
		closing_conn = connection ? bt_conn_ref(connection) : NULL;
	}
	k_mutex_unlock(&state_lock);
	if (faulted) { return; }
	int err = 0;
	if (!wanted && enabled) {
		err = bt_le_adv_stop();
		if (err && err != -EALREADY) { printk("BLE: shutdown advertising error=%d\n", err); }
		if (closing_conn) {
			/* If this thread won ownership before the expiry handler, it must
			 * request disconnect itself. DISCONNECTING safely returns zero. */
			err = bt_conn_disconnect(closing_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			if (err && err != -ENOTCONN) { printk("BLE: shutdown disconnect error=%d\n", err); }
			bt_conn_unref(closing_conn);
		}
		/* HCI command acceptance does not mean LL_TERMINATE was sent.
		 * Let disconnection-complete/CCC cleanup/recycled run before reset;
		 * an absent/unresponsive peer must not keep the controller awake. */
		if (wait_for_connection_cleanup()) {
			printk("BLE: graceful disconnect timed out after 1000 ms; forcing shutdown\n");
		}
		/* Force close also cleans any links left after the bounded grace. */
		err = bt_disable();
		if (!err) { err = wait_for_connection_cleanup(); }
		if (!err) { printk("BLE: stack/controller/MPSL disabled between windows\n"); }
	} else if (wanted && !enabled) {
		err = wait_for_connection_cleanup();
#if !defined(CONFIG_ENV_TELEMETRY_SECURE)
		if (!err && identity_saved) {
			err = bt_id_create(&identity, NULL);
			if (err == BT_ID_DEFAULT) { err = 0; }
		}
#endif
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
		if (!err) { err = enable_with_settings(); }
#else
		if (!err) { err = bt_enable(NULL); }
#endif
		if (!err) {
			k_mutex_lock(&state_lock, K_FOREVER);
			stack_enabled = true;
			if (active_until_ms) {
				/* Give the receiver the full window after synchronous startup. */
				active_until_ms = k_uptime_get() + configuration.active_window_ms;
				active_until_ms = MAX(active_until_ms, transfer_hold_until_ms);
				active_until_ms = MAX(active_until_ms, history_hold_until_ms);
				active_until_ms = MAX(active_until_ms, notify_hold_until_ms);
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
				active_until_ms = MAX(active_until_ms, enrollment_until_ms);
#endif
				k_work_reschedule(&window_expiry_work, K_MSEC(active_until_ms - k_uptime_get()));
			}
			k_mutex_unlock(&state_lock);
			printk("BLE: stack/controller/MPSL enabled for publication\n");
		}
	}
	if (err) {
		k_mutex_lock(&state_lock, K_FOREVER);
		stack_faulted = true;
		active_until_ms = 0;
		k_mutex_unlock(&state_lock);
		printk("ERROR: BLE stack lifecycle=%d; stopped until reboot\n", err);
	}
}

#if defined(CONFIG_ENV_TELEMETRY_SECURE)
static void verify_owner_bond(void)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	bool pending = verify_bond_pending && stack_enabled && !expiry_disconnect_pending;
	if (repair_pending || discard_bond_pending) { verify_bond_pending = false; pending = false; }
	k_mutex_unlock(&state_lock);
	if (!pending) { return; }
	int err = verify_stored_key_count(1);
	k_mutex_lock(&state_lock, K_FOREVER);
	verify_bond_pending = false;
	bool accepted = !err && enrollment_until_ms > k_uptime_get() &&
	                !repair_pending && !discard_bond_pending;
	if (accepted) {
		enrollment_until_ms = 0;
		new_pin_pending = false;
		pin_display_pending = false;
		active_until_ms = k_uptime_get() + configuration.active_window_ms;
		k_work_reschedule(&window_expiry_work, K_MSEC(configuration.active_window_ms));
	} else {
		discard_bond_pending = true;
		active_until_ms = k_uptime_get();
		k_work_reschedule(&window_expiry_work, K_NO_WAIT);
	}
	k_mutex_unlock(&state_lock);
	if (accepted) {
		printk("BLE SECURITY: saved authenticated SC owner verified; normal minute windows resumed\n");
		pairing_event(ENV_TELEMETRY_PAIRING_CLOSED, 0, pin_request_id);
	} else {
		printk("BLE SECURITY: persistent bond verification failed error=%d; removing attempt\n", err);
		k_sem_give(&updated);
	}
}

/* Sender thread owns these operations. Never block the system queue while
 * waiting for disconnection, storage, entropy or a physical panel refresh. */
static void prepare_enrollment(void)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	bool reset = repair_pending;
	bool discard = discard_bond_pending;
	bool prepare = new_pin_pending;
	if (!stack_enabled || stack_faulted || expiry_disconnect_pending || (!reset && !discard && !prepare)) {
		k_mutex_unlock(&state_lock);
		return;
	}
	k_work_cancel_delayable(&window_expiry_work);
	k_work_cancel_delayable(&advertising_work);
	/* Serialize the expiry handler's HCI request with this disconnect. */
	expiry_disconnect_pending = true;
	struct bt_conn *conn = connection ? bt_conn_ref(connection) : NULL;
	pairing_attempt = false;
	k_mutex_unlock(&state_lock);
	int err = bt_le_adv_stop();
	if (err == -EALREADY) { err = 0; }
	if (!err && conn) {
		err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err == -ENOTCONN) { err = 0; }
	}
	if (conn) { bt_conn_unref(conn); }
	if (!err) { err = wait_for_connection_cleanup(); }
	if (!err && (reset || discard)) { err = bt_unpair(BT_ID_DEFAULT, NULL); }
	if (!err && (reset || discard)) { err = verify_stored_key_count(0); }
	uint32_t random = 0;
	if (!err && (!discard || reset)) {
		/* Rejection sampling avoids the modulo bias of the 32-bit source. */
		do { err = bt_rand(&random, sizeof(random)); }
		while (!err && (random >= 4294000000U ||
		       (pin_request_id && random % 1000000U == prepared_pin)));
	}
	k_mutex_lock(&state_lock, K_FOREVER);
	expiry_disconnect_pending = false;
	repair_pending = false;
	discard_bond_pending = false;
	new_pin_pending = false;
	if (err || (discard && !reset)) {
		enrollment_until_ms = 0;
		active_until_ms = 0;
		pin_available = false;
		pin_display_pending = false;
		/* bt_unpair also ignores its storage delete error in this SDK.
		 * A failed deletion may not return to owner reception next minute. */
		if (err && (reset || discard)) { stack_faulted = true; }
	} else {
		prepared_pin = random % 1000000U;
		++pin_request_id;
		pin_available = true;
		pin_display_pending = true;
		enrollment_until_ms = k_uptime_get() + configuration.pairing_window_ms;
		active_until_ms = enrollment_until_ms;
		k_work_reschedule(&window_expiry_work, K_MSEC(configuration.pairing_window_ms));
	}
	uint32_t request = pin_request_id;
	uint32_t pin = prepared_pin;
	k_mutex_unlock(&state_lock);
	if (err || (discard && !reset)) {
		printk("BLE SECURITY: enrollment closed error=%d late_bond_discarded=%u\n", err, discard);
		pairing_event(ENV_TELEMETRY_PAIRING_CLOSED, 0, request);
		if (err && (reset || discard)) {
			(void)bt_disable();
			k_mutex_lock(&state_lock, K_FOREVER);
			stack_enabled = false;
			k_mutex_unlock(&state_lock);
		}
		k_sem_give(&updated);
	} else {
		if (reset) { printk("BLE SECURITY: owner bonds deleted by physical PUSH hold\n"); }
		pairing_event(ENV_TELEMETRY_PAIRING_OPENED, 0, request);
		pairing_event(ENV_TELEMETRY_PAIRING_PASSKEY, pin, request);
		printk("BLE SECURITY: fresh PIN rendering request=%u; advertising waits for display\n", request);
	}
}
#endif

static void notify_latest(void)
{
	uint8_t frame[ENV_TELEMETRY_MAX_SIZE];
	uint8_t fragment[247];
	k_mutex_lock(&state_lock, K_FOREVER);
	struct bt_conn *conn = connection && radio_active_locked() ? bt_conn_ref(connection) : NULL;
	size_t length = latest_length;
	memcpy(frame, latest, length);
	k_mutex_unlock(&state_lock);
	if (!conn) {
		return;
	}
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	if (bt_conn_get_security(conn) != BT_SECURITY_L4) {
		bt_conn_unref(conn);
		return;
	}
#endif
	if (bt_gatt_is_subscribed(conn, &environment_service.attrs[2], BT_GATT_CCC_NOTIFY)) {
		/* ATT buffers/fragment transmission may start near a window boundary.
		 * Give this snapshot a fresh drain budget without replacing an image
		 * upload's longer, independent connection lease. */
		k_mutex_lock(&state_lock, K_FOREVER);
		if (configuration.active_window_ms && connection == conn && radio_active_locked()) {
			notify_hold_until_ms = k_uptime_get() + 5000;
			active_until_ms = MAX(active_until_ms, notify_hold_until_ms);
			active_until_ms = MAX(active_until_ms, transfer_hold_until_ms);
			active_until_ms = MAX(active_until_ms, history_hold_until_ms);
			k_work_reschedule(&window_expiry_work, K_MSEC(active_until_ms - k_uptime_get()));
		}
		k_mutex_unlock(&state_lock);
		size_t capacity = MIN(sizeof(fragment), bt_gatt_get_mtu(conn) - 3);
		size_t fragment_count = 0;
		size_t offset = 0;
		for (; offset < length;) {
			k_mutex_lock(&state_lock, K_FOREVER);
			bool active = radio_active_locked();
			k_mutex_unlock(&state_lock);
			if (!active) { break; }
			size_t count = env_telemetry_fragment(frame, length, offset, fragment, capacity);
			if (!count) { break; }
			/* Dedicated thread: waiting for ATT buffers cannot block I2C sampling. */
			int err = bt_gatt_notify(conn, &environment_service.attrs[2], fragment, count);
			if (err != 0) {
				printk("BLE notify skipped: %d\n", err);
				break;
			}
			offset += count - ENV_TELEMETRY_FRAGMENT_HEADER_SIZE;
			++fragment_count;
		}
		if (offset == length && length && configuration.log_publications) {
			printk("BLE: sent sequence=%u bytes=%u fragments=%u\n",
			       frame_sequence(frame), (unsigned int)length, (unsigned int)fragment_count);
		}
	}
	bt_conn_unref(conn);
}

static void notify_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (;;) {
		k_sem_take(&updated, K_FOREVER);
		update_stack_lifecycle();
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
		verify_owner_bond();
		prepare_enrollment();
#endif
		if (configuration.disable_between_windows) { start_advertising(); }
		notify_latest();
	}
}
K_THREAD_DEFINE(env_telemetry_sender, 3072, notify_thread, NULL, NULL, NULL, 7, 0, 0);

int env_telemetry_ble_init(const struct env_telemetry_ble_config *config)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	if (started) {
		k_mutex_unlock(&state_lock);
		return -EALREADY;
	}
	configuration = config ? *config : (struct env_telemetry_ble_config){0};
	if (!configuration.advertising_interval_min) { configuration.advertising_interval_min = 1600; }
	if (!configuration.advertising_interval_max) { configuration.advertising_interval_max = 1600; }
	bool valid = configuration.advertising_interval_min >= 0x20 &&
		configuration.advertising_interval_min <= configuration.advertising_interval_max &&
		configuration.advertising_interval_max <= 0x4000 &&
		(!configuration.disable_between_windows || configuration.active_window_ms != 0);
	k_mutex_unlock(&state_lock);
	if (!valid) { printk("ERROR: BLE init advertising/window config invalid\n"); return -EINVAL; }
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	if (!configuration.disable_between_windows || !configuration.pairing_event ||
	    configuration.pairing_window_ms < 60000) {
		printk("ERROR: BLE init secure callback/window config invalid\n"); return -EINVAL;
	}
	printk("BLE SECURITY: initializing internal settings backend\n");
	int err = settings_subsys_init();
	if (err) { printk("ERROR: BLE init settings backend=%d\n", err); return err; }
	err = bt_conn_auth_cb_register(&authentication);
	if (err) { printk("ERROR: BLE init auth callbacks=%d\n", err); return err; }
	err = bt_conn_auth_info_cb_register(&authentication_info);
	if (err) { printk("ERROR: BLE init auth info callbacks=%d\n", err); return err; }
	printk("BLE SECURITY: settings backend and authentication callbacks ready\n");
#else
	int err;
#endif
	if (configuration.disable_between_windows &&
	    (!IS_ENABLED(CONFIG_BT_UNINIT_MPSL_ON_DISABLE)
#if !defined(CONFIG_ENV_TELEMETRY_SECURE)
	     ||
	     IS_ENABLED(CONFIG_BT_SETTINGS) || IS_ENABLED(CONFIG_BT_PRIVACY) || IS_ENABLED(CONFIG_BT_SMP))) {
#else
	     )) {
#endif
		/* Identity/keys for bonded/privacy peripherals need a settings owner. */
		printk("ERROR: BLE init unsupported controller shutdown config\n");
		return -ENOTSUP;
	}
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	err = enable_with_settings();
	unsigned int bonds = 0;
	if (!err) { bt_foreach_bond(BT_ID_DEFAULT, count_bond, &bonds); }
#else
	err = bt_enable(NULL);
#endif
	if (!err && configuration.disable_between_windows) {
#if !defined(CONFIG_ENV_TELEMETRY_SECURE)
		size_t count = 1;
		bt_id_get(&identity, &count);
		identity_saved = count == 1;
#endif
		/* MPSL SYS_INIT already runs at boot. Complete one normal init/close
		 * cycle now so it is also inactive before the first actual sample. */
		err = bt_disable();
		if (!err) { err = wait_for_connection_cleanup(); }
#if !defined(CONFIG_ENV_TELEMETRY_SECURE)
		if (!err && !identity_saved) { err = -EIO; }
#endif
		if (!err) { printk("BLE: initial stack/controller/MPSL disabled\n"); }
	}
	if (err == 0) {
		k_mutex_lock(&state_lock, K_FOREVER);
		started = true;
		stack_enabled = !configuration.disable_between_windows;
		stack_faulted = false;
		active_until_ms = 0;
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
		if (!bonds) {
			enrollment_until_ms = k_uptime_get() + configuration.pairing_window_ms;
			new_pin_pending = true;
		}
#endif
		bool continuous = configuration.active_window_ms == 0;
		if (continuous) { k_work_reschedule(&advertising_work, K_NO_WAIT); }
		k_mutex_unlock(&state_lock);
		if (!continuous) {
			printk("BLE: paused until publication; window_ms=%u\n", configuration.active_window_ms);
		}
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
		if (!bonds) {
			pairing_event(ENV_TELEMETRY_PAIRING_OPENED, 0, 0);
			printk("BLE SECURITY: no owner; initial enrollment will show a PIN before first advertisement\n");
		}
#endif
	}
	if (err) { printk("ERROR: BLE init enable/settings/initial shutdown=%d\n", err); }
	return err;
}

int env_telemetry_ble_publish_frame(const uint8_t *frame, size_t length)
{
	if (!frame || length < ENV_TELEMETRY_HEADER_SIZE || length > ENV_TELEMETRY_MAX_SIZE ||
	    frame[0] != 'O' || frame[1] != 'E' || frame[2] != 1 ||
	    frame[3] != ENV_TELEMETRY_HEADER_SIZE ||
	    ((size_t)frame[4] | (size_t)frame[5] << 8) != length || frame[10] || frame[11]) {
		return -EINVAL;
	}
	k_mutex_lock(&state_lock, K_FOREVER);
	memcpy(latest, frame, length);
	latest_length = length;
	if (stack_faulted) {
		k_mutex_unlock(&state_lock);
		return -EIO;
	}
	bool ready = started;
	bool windowed = ready && configuration.active_window_ms != 0;
	if (windowed) {
		active_until_ms = k_uptime_get() + configuration.active_window_ms;
		active_until_ms = MAX(active_until_ms, transfer_hold_until_ms);
		active_until_ms = MAX(active_until_ms, history_hold_until_ms);
		active_until_ms = MAX(active_until_ms, notify_hold_until_ms);
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
		active_until_ms = MAX(active_until_ms, enrollment_until_ms);
#endif
		k_work_reschedule(&window_expiry_work, K_MSEC(active_until_ms - k_uptime_get()));
		k_work_reschedule(&advertising_work, K_NO_WAIT);
	}
	struct bt_conn *conn = connection ? bt_conn_ref(connection) : NULL;
	k_mutex_unlock(&state_lock);
	if (windowed) {
		printk("BLE: window opened sequence=%u duration_ms=%u\n",
		       frame_sequence(frame), configuration.active_window_ms);
	}
	if (ready && configuration.log_publications) {
		bool subscribed = conn && bt_gatt_is_subscribed(conn, &environment_service.attrs[2], BT_GATT_CCC_NOTIFY);
		printk("BLE: cached sequence=%u bytes=%u connected=%u subscribed=%u\n",
		       frame_sequence(frame), (unsigned int)length, conn != NULL, subscribed);
	}
	if (conn) { bt_conn_unref(conn); }
	if (ready) { k_sem_give(&updated); }
	return 0;
}

int env_telemetry_ble_publish(const struct env_telemetry_sample *sample)
{
	uint8_t frame[ENV_TELEMETRY_MAX_SIZE];
	size_t length = env_telemetry_encode(sample, frame, sizeof(frame));
	if (!length) { return -EINVAL; }
	return env_telemetry_ble_publish_frame(frame, length);
}

int env_telemetry_ble_repair(void)
{
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	k_mutex_lock(&state_lock, K_FOREVER);
	int err = !started ? -EACCES : stack_faulted ? -EIO : 0;
	if (!err) {
		repair_pending = true;
		transfer_hold_until_ms = 0;
		history_hold_until_ms = 0;
		notify_hold_until_ms = 0;
		pin_available = false;
		pin_display_pending = false;
		/* Stop any current link independently of blocked notification code. */
		active_until_ms = k_uptime_get();
		k_work_reschedule(&window_expiry_work, K_NO_WAIT);
	}
	k_mutex_unlock(&state_lock);
	if (!err) { k_sem_give(&updated); }
	return err;
#else
	return -ENOTSUP;
#endif
}

static int hold_auxiliary(struct bt_conn *conn, uint32_t duration_ms, bool history, bool replace)
{
	if (!conn || !duration_ms || duration_ms > (history ? 300000U : 120000U)) { return -EINVAL; }
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	if (bt_conn_get_security(conn) != BT_SECURITY_L4) { return -EACCES; }
#endif
	k_mutex_lock(&state_lock, K_FOREVER);
	int error = connection != conn || !radio_active_locked() ? -ENOTCONN : 0;
	if (!error && configuration.active_window_ms) {
		int64_t *deadline = history ? &history_hold_until_ms : &transfer_hold_until_ms;
		*deadline = replace ? k_uptime_get() + duration_ms : MAX(*deadline, k_uptime_get() + duration_ms);
		if (replace) {
			active_until_ms = MAX(k_uptime_get() + configuration.active_window_ms, notify_hold_until_ms);
			active_until_ms = MAX(active_until_ms, transfer_hold_until_ms);
			active_until_ms = MAX(active_until_ms, history_hold_until_ms);
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
			active_until_ms = MAX(active_until_ms, enrollment_until_ms);
#endif
		} else { active_until_ms = MAX(active_until_ms, *deadline); }
		k_work_reschedule(&window_expiry_work, K_MSEC(active_until_ms - k_uptime_get()));
	}
	k_mutex_unlock(&state_lock);
	return error;
}

static void release_auxiliary(struct bt_conn *conn, bool history)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	if (conn && connection == conn) {
		if (history) { history_hold_until_ms = 0; }
		else { transfer_hold_until_ms = 0; }
		if (configuration.active_window_ms && radio_active_locked()) {
			active_until_ms = MAX(k_uptime_get() + configuration.active_window_ms, notify_hold_until_ms);
			active_until_ms = MAX(active_until_ms, transfer_hold_until_ms);
			active_until_ms = MAX(active_until_ms, history_hold_until_ms);
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
			active_until_ms = MAX(active_until_ms, enrollment_until_ms);
#endif
			k_work_reschedule(&window_expiry_work, K_MSEC(active_until_ms - k_uptime_get()));
		}
	}
	k_mutex_unlock(&state_lock);
}

int env_telemetry_ble_hold_connection(struct bt_conn *conn, uint32_t duration_ms)
{
	return hold_auxiliary(conn, duration_ms, false, false);
}

void env_telemetry_ble_release_connection(struct bt_conn *conn)
{
	release_auxiliary(conn, false);
}

int env_telemetry_ble_hold_history(struct bt_conn *conn, uint32_t duration_ms)
{
	return hold_auxiliary(conn, duration_ms, true, false);
}

int env_telemetry_ble_set_history_lease(struct bt_conn *conn, uint32_t duration_ms)
{
	return hold_auxiliary(conn, duration_ms, true, true);
}

void env_telemetry_ble_release_history(struct bt_conn *conn)
{
	release_auxiliary(conn, true);
}

void env_telemetry_ble_pairing_display_ready(uint32_t request_id, int error)
{
#if defined(CONFIG_ENV_TELEMETRY_SECURE)
	k_mutex_lock(&state_lock, K_FOREVER);
	bool current = request_id == pin_request_id && pin_display_pending &&
	               pin_available && !repair_pending && enrollment_until_ms > k_uptime_get();
	if (current) {
		pin_display_pending = false;
		if (error) {
			pin_available = false;
			enrollment_until_ms = 0;
			active_until_ms = 0;
		} else {
			enrollment_until_ms = k_uptime_get() + configuration.pairing_window_ms;
			active_until_ms = enrollment_until_ms;
			k_work_reschedule(&window_expiry_work, K_MSEC(configuration.pairing_window_ms));
		}
	}
	k_mutex_unlock(&state_lock);
	if (current) {
		printk("BLE SECURITY: PIN display completed request=%u error=%d; enrollment_ms=%u\n",
		       request_id, error, configuration.pairing_window_ms);
		if (error) { pairing_event(ENV_TELEMETRY_PAIRING_CLOSED, 0, request_id); }
		k_sem_give(&updated);
	}
#else
	ARG_UNUSED(request_id); ARG_UNUSED(error);
#endif
}
