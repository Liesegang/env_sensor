#include <telemetry.h>
#include <outdoor_clock.h>

#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(
	TELEMETRY_SERVICE_UUID_BYTES);
static struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(
	TELEMETRY_CHARACTERISTIC_UUID_BYTES);
static uint8_t latest[TELEMETRY_MAX_SIZE];
static size_t latest_length;
/* Freeze offset-based ATT long reads while newer notifications are published. */
static uint8_t read_frame[TELEMETRY_MAX_SIZE];
static size_t read_length;
static struct bt_conn *connection;
static bool started;
K_MUTEX_DEFINE(state_lock);
K_SEM_DEFINE(updated, 0, 1);

static ssize_t read_snapshot(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buffer, uint16_t len, uint16_t offset)
{
	k_mutex_lock(&state_lock, K_FOREVER);
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
	int err = outdoor_clock_set_packet(buffer, len);
	if (err) { return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED); }
	printk("CLOCK: synchronized from BLE browser time\n");
	return len;
}

BT_GATT_SERVICE_DEFINE(outdoor_service,
	BT_GATT_PRIMARY_SERVICE(&service_uuid),
	BT_GATT_CHARACTERISTIC(&data_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_snapshot, write_clock, NULL),
	BT_GATT_CCC(subscription_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static const struct bt_data advertisement[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		TELEMETRY_SERVICE_UUID_BYTES),
};
static const struct bt_data scan_response[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void advertise(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(advertising_work, advertise);

static void advertise(struct k_work *work)
{
	ARG_UNUSED(work);
	k_mutex_lock(&state_lock, K_FOREVER);
	bool connected = connection != NULL;
	k_mutex_unlock(&state_lock);
	if (connected || !started) {
		return;
	}
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, advertisement, ARRAY_SIZE(advertisement),
				 scan_response, ARRAY_SIZE(scan_response));
	if (err != 0 && err != -EALREADY) {
		printk("ERROR: BLE advertise: %d\n", err);
		k_work_reschedule(&advertising_work, K_MSEC(500));
	} else {
		printk("BLE advertising: %s service=%s\n", CONFIG_BT_DEVICE_NAME, TELEMETRY_SERVICE_UUID);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		printk("ERROR: BLE connection: %u\n", err);
		return;
	}
	k_mutex_lock(&state_lock, K_FOREVER);
	connection = bt_conn_ref(conn);
	k_mutex_unlock(&state_lock);
	printk("BLE connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	if (connection == conn) {
		bt_conn_unref(connection);
		connection = NULL;
	}
	k_mutex_unlock(&state_lock);
	printk("BLE disconnected: %u\n", reason);
}

static void recycled(void)
{
	/* The sole connection slot must be free before connectable advertising resumes. */
	k_work_reschedule(&advertising_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

static void notify_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint8_t frame[TELEMETRY_MAX_SIZE];
	uint8_t fragment[247];
	for (;;) {
		k_sem_take(&updated, K_FOREVER);
		k_mutex_lock(&state_lock, K_FOREVER);
		struct bt_conn *conn = connection ? bt_conn_ref(connection) : NULL;
		size_t length = latest_length;
		memcpy(frame, latest, length);
		k_mutex_unlock(&state_lock);
		if (!conn) {
			continue;
		}
		if (bt_gatt_is_subscribed(conn, &outdoor_service.attrs[2], BT_GATT_CCC_NOTIFY)) {
			size_t capacity = MIN(sizeof(fragment), bt_gatt_get_mtu(conn) - 3);
			for (size_t offset = 0; offset < length;) {
				size_t count = telemetry_fragment(frame, length, offset, fragment, capacity);
				if (!count) { break; }
				/* Dedicated thread: waiting for ATT buffers cannot block I2C sampling. */
				int err = bt_gatt_notify(conn, &outdoor_service.attrs[2], fragment, count);
				if (err != 0) {
					printk("BLE notify skipped: %d\n", err);
					break;
				}
				offset += count - TELEMETRY_FRAGMENT_HEADER_SIZE;
			}
		}
		bt_conn_unref(conn);
	}
}
K_THREAD_DEFINE(telemetry_sender, 3072, notify_thread, NULL, NULL, NULL, 7, 0, 0);

int telemetry_ble_init(void)
{
	struct telemetry_sample initial = {
		.uptime_ms = k_uptime_get(), .enabled_mask = 0x0f,
		.opt4001.error = -EAGAIN, .bme690.error = -EAGAIN,
		.sgp41.error = -EAGAIN, .stcc4.error = -EAGAIN,
		.bmv080.error = -EAGAIN, .as3935.error = -EAGAIN, .sfa40.error = -EAGAIN,
	};
#if defined(CONFIG_APP_BMV080)
	initial.enabled_mask |= 0x10;
#endif
#if defined(CONFIG_APP_AS3935)
	initial.enabled_mask |= 0x20;
#endif
#if defined(CONFIG_APP_SFA40)
	initial.enabled_mask |= 0x40;
#endif
	telemetry_ble_publish(&initial);
	int err = bt_enable(NULL);
	if (err == 0) {
		started = true;
		k_work_reschedule(&advertising_work, K_NO_WAIT);
	}
	return err;
}

void telemetry_ble_publish(const struct telemetry_sample *sample)
{
	uint8_t frame[TELEMETRY_MAX_SIZE];
	size_t length = telemetry_encode(sample, frame, sizeof(frame));
	if (!length) {
		printk("ERROR: BLE packet encoding failed\n");
		return;
	}
	k_mutex_lock(&state_lock, K_FOREVER);
	memcpy(latest, frame, length);
	latest_length = length;
	k_mutex_unlock(&state_lock);
	if (started) { k_sem_give(&updated); }
}
