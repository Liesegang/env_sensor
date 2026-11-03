#include "card_history.h"
#include "card_ble.h"
#include "sd_logger.h"
#include "card_sample.h"
#include <env_history.h>
#include <env_telemetry_ble.h>
#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define HISTORY_PERMISSIONS (BT_GATT_PERM_READ_LESC | BT_GATT_PERM_WRITE_LESC | \
    BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN)
#define HISTORY_UUID(n) BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x722acd00U+(n),0xe77f,0x468d,0xa916,0x67e599e7cffdULL))
static struct bt_uuid_128 history_uuid = HISTORY_UUID(0xa0);
static struct bt_uuid_128 control_uuid = HISTORY_UUID(0xa1);
static struct bt_uuid_128 status_uuid = HISTORY_UUID(0xa2);
static struct bt_uuid_128 data_uuid = HISTORY_UUID(0xa3);
K_MUTEX_DEFINE(history_lock);
K_SEM_DEFINE(history_requested, 0, 1);
static struct k_thread history_thread;
K_THREAD_STACK_DEFINE(history_stack, 5120);
static struct bt_conn *owner;
static bool initialized, worker_busy, pull_pending, hint_pending;
static bool parameters_requested;
static uint32_t generation;
static struct env_history_control pending_control, page_control;
static struct env_history_status status;
static uint8_t page_bytes[ENV_HISTORY_MAX_PAGE_SIZE];
static uint8_t status_snapshot[ENV_HISTORY_STATUS_SIZE];
static size_t page_length, read_progress;
static uint64_t page_boot_id;
static bool page_read_complete;
static uint32_t last_ack_request;
static struct env_history_cursor last_ack;
static int64_t lease_deadline;
static void history_expired(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(history_timeout, history_expired);

static bool same_cursor(struct env_history_cursor a, struct env_history_cursor b)
{
    return a.boot_id == b.boot_id && a.sequence == b.sequence;
}

static void clear_page_locked(void)
{
    page_length = read_progress = 0;
    page_read_complete = false;
    status.page_record_count = status.page_size = 0;
    status.page_last_sequence = 0;
}

static void release_owner_locked(void)
{
    if (owner) {
        /* The shared transport preserves a 5-second ATT/notification grace,
         * and an independent image lease, after this operation completes.
         */
        env_telemetry_ble_release_history(owner);
        bt_conn_unref(owner); owner = NULL;
    }
    (void)k_work_cancel_delayable(&history_timeout);
    parameters_requested = false;
}

static int set_deadline_locked(struct bt_conn *conn, uint32_t duration_ms)
{
    int error = env_telemetry_ble_set_history_lease(conn, duration_ms);
    if (!error) {
        lease_deadline = k_uptime_get() + duration_ms;
        (void)k_work_reschedule(&history_timeout, K_MSEC(duration_ms));
    }
    return error;
}

static int renew_locked(struct bt_conn *conn)
{
    if (status.state == ENV_HISTORY_STATE_PREPARING) {
        /* Metadata polling cannot extend a stuck SD operation indefinitely. */
        int64_t remaining = lease_deadline - k_uptime_get();
        return remaining > 0 ? env_telemetry_ble_set_history_lease(conn, (uint32_t)remaining) : -ETIMEDOUT;
    }
    return set_deadline_locked(conn, 120000);
}

static void history_expired(struct k_work *work)
{
    ARG_UNUSED(work);
    k_mutex_lock(&history_lock, K_FOREVER);
    if (owner) {
        int64_t remaining = lease_deadline - k_uptime_get();
        if (remaining > 0) { (void)k_work_reschedule(&history_timeout, K_MSEC(remaining)); }
        else {
            ++generation; status.state = ENV_HISTORY_STATE_ERROR; status.error = -ETIMEDOUT;
            if (pull_pending) { pull_pending = false; worker_busy = false; }
            clear_page_locked(); release_owner_locked();
        }
    }
    k_mutex_unlock(&history_lock);
}

static void notify_status(void);

static ssize_t write_control(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buffer, uint16_t length, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(attr);
    if (bt_conn_get_security(conn) != BT_SECURITY_L4) { return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION); }
    if (offset || flags) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    struct env_history_control command;
    int error = env_history_decode_control(buffer, length, &command);
    if (error < 0) { return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED); }
    k_mutex_lock(&history_lock, K_FOREVER);
    error = !initialized ? -EACCES : owner && owner != conn ? -EBUSY : 0;
    if (!error && command.opcode == ENV_HISTORY_OP_ACK &&
        command.request_id == last_ack_request && same_cursor(command.cursor, last_ack) &&
        !(status.state == ENV_HISTORY_STATE_READY && status.request_id == command.request_id &&
          page_boot_id == command.cursor.boot_id && status.page_last_sequence == command.cursor.sequence)) {
        /* A lost ACK response must not make the receiver repeat committed data.
         * This changes neither a newer in-flight page nor its connection lease.
         */
        k_mutex_unlock(&history_lock); return length;
    }
    if (!error && command.opcode == ENV_HISTORY_OP_PULL) {
        if (status.state == ENV_HISTORY_STATE_READY) {
            error = command.request_id == page_control.request_id &&
                same_cursor(command.cursor, page_control.cursor) ? renew_locked(conn) : -EBUSY;
            if (!error) { hint_pending = true; }
        } else if (worker_busy) { error = -EBUSY; }
        else {
            error = set_deadline_locked(conn, 300000);
            if (!error) {
                if (!owner) { owner = bt_conn_ref(conn); }
                pending_control = page_control = command;
                ++generation; pull_pending = worker_busy = true;
                status.state = ENV_HISTORY_STATE_PREPARING;
                status.request_id = command.request_id; status.error = 0; status.flags = 0;
                clear_page_locked();
            }
        }
    } else if (!error && command.opcode == ENV_HISTORY_OP_ACK) {
        struct env_history_cursor expected = {page_boot_id, status.page_last_sequence};
        error = owner != conn || status.state != ENV_HISTORY_STATE_READY || !page_read_complete ||
            command.request_id != status.request_id || !same_cursor(command.cursor, expected) ? -EINVAL : 0;
        if (!error) {
            last_ack_request = command.request_id; last_ack = command.cursor;
            bool more = (status.flags & ENV_HISTORY_FLAG_MORE) != 0;
            status.state = more ? ENV_HISTORY_STATE_IDLE : ENV_HISTORY_STATE_COMPLETE;
            clear_page_locked(); hint_pending = true;
            if (more) { error = renew_locked(conn); }
            else { release_owner_locked(); }
        }
    } else if (!error && command.opcode == ENV_HISTORY_OP_CANCEL) {
        ++generation; status.state = ENV_HISTORY_STATE_IDLE; status.error = 0;
        status.request_id = command.request_id; status.flags = 0;
        if (pull_pending) { pull_pending = false; worker_busy = false; }
        clear_page_locked(); release_owner_locked(); hint_pending = true;
    }
    bool wake = !error && (pull_pending || hint_pending);
    k_mutex_unlock(&history_lock);
    if (wake) { k_sem_give(&history_requested); }
    return error ? BT_GATT_ERR(error == -EBUSY ? BT_ATT_ERR_INSUFFICIENT_RESOURCES : BT_ATT_ERR_VALUE_NOT_ALLOWED) : length;
}

static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buffer, uint16_t length, uint16_t offset)
{
    if (bt_conn_get_security(conn) != BT_SECURITY_L4) { return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION); }
    k_mutex_lock(&history_lock, K_FOREVER);
    int error = owner && owner != conn ? -EBUSY : 0;
    if (!error) {
        bool terminal = status.state == ENV_HISTORY_STATE_COMPLETE || status.state == ENV_HISTORY_STATE_ERROR;
        if (terminal) {
            /* A final metadata/ReadBlob response must not reopen a two-minute
             * session after ACK. Keep only the short ATT response grace.
             */
            if (owner) { release_owner_locked(); }
            error = env_telemetry_ble_hold_history(conn, 5000);
        } else {
            error = renew_locked(conn);
            if (!error && !owner) { owner = bt_conn_ref(conn); }
        }
    }
    if (!error && !offset) {
        struct card_ble_clock clock; card_ble_clock_snapshot(&clock);
        status.current_boot_id = clock.boot_id;
        status.clock_revision = clock.revision;
        status.utc_anchor_s = clock.unix_anchor_s;
        status.uptime_anchor_s = clock.uptime_anchor_s;
        error = env_history_encode_status(&status, status_snapshot, sizeof(status_snapshot));
        if (error >= 0) { error = 0; }
    }
    ssize_t result = error ? BT_GATT_ERR(BT_ATT_ERR_UNLIKELY) :
        bt_gatt_attr_read(conn, attr, buffer, length, offset, status_snapshot, sizeof(status_snapshot));
    k_mutex_unlock(&history_lock); return result;
}

static ssize_t read_data(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buffer, uint16_t length, uint16_t offset)
{
    if (bt_conn_get_security(conn) != BT_SECURITY_L4) { return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION); }
    k_mutex_lock(&history_lock, K_FOREVER);
    int error = owner != conn || status.state != ENV_HISTORY_STATE_READY ? -EACCES : renew_locked(conn);
    ssize_t result = error ? BT_GATT_ERR(BT_ATT_ERR_UNLIKELY) :
        bt_gatt_attr_read(conn, attr, buffer, length, offset, page_bytes, page_length);
    if (!error && result >= 0) {
        if (!offset) { read_progress = (size_t)result; page_read_complete = false; }
        else if (offset == read_progress) { read_progress += (size_t)result; }
        if (read_progress == page_length) { page_read_complete = true; }
    }
    k_mutex_unlock(&history_lock); return result;
}

static void status_ccc(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr); ARG_UNUSED(value);
}

/* Lexicographic static service order is deliberate: existing OE/image GATT
 * handles stay unchanged on bonded phones when this service is appended.
 */
BT_GATT_SERVICE_DEFINE(zzz_card_history_service,
    BT_GATT_PRIMARY_SERVICE(&history_uuid),
    BT_GATT_CHARACTERISTIC(&control_uuid.uuid, BT_GATT_CHRC_WRITE, HISTORY_PERMISSIONS, NULL, write_control, NULL),
    BT_GATT_CHARACTERISTIC(&status_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, HISTORY_PERMISSIONS, read_status, NULL, NULL),
    BT_GATT_CCC(status_ccc, HISTORY_PERMISSIONS),
    BT_GATT_CHARACTERISTIC(&data_uuid.uuid, BT_GATT_CHRC_READ, HISTORY_PERMISSIONS, read_data, NULL, NULL)
);

static void notify_status(void)
{
    uint8_t hint[ENV_HISTORY_HINT_SIZE];
    k_mutex_lock(&history_lock, K_FOREVER);
    struct bt_conn *conn = owner ? bt_conn_ref(owner) : NULL;
    int count = env_history_encode_hint(status.state, status.request_id, hint, sizeof(hint));
    hint_pending = false;
    k_mutex_unlock(&history_lock);
    if (conn) {
        if (count > 0 && bt_gatt_is_subscribed(conn, &zzz_card_history_service.attrs[4], BT_GATT_CCC_NOTIFY)) {
            (void)bt_gatt_notify(conn, &zzz_card_history_service.attrs[4], hint, (uint16_t)count);
        }
        bt_conn_unref(conn);
    }
}

static struct env_history_record history_record(const struct card_sample *sample, uint64_t boot,
                                                const struct card_ble_clock *clock)
{
    struct env_history_record record = {
        .sequence=sample->sequence, .uptime_s=sample->uptime_s, .unix_s=sample->unix_s,
        .temperature_mc=sample->temperature_mc, .humidity_mpercent=sample->humidity_mpercent,
        .hcho_millippb=sample->hcho_millippb, .bme_temperature_mc=sample->bme_temperature_mc,
        .bme_humidity_mpercent=sample->bme_humidity_mpercent, .pressure_pa=sample->pressure_pa,
        .gas_resistance_ohm=sample->gas_resistance_ohm, .millilux=sample->millilux,
        .co2_ppm=sample->co2_ppm, .raw_voc=sample->raw_voc, .raw_nox=sample->raw_nox,
        .voc_index=sample->voc_index, .nox_index=sample->nox_index, .stcc_status=sample->stcc_status,
        .stcc_self_test=sample->stcc_self_test, .valid=sample->valid, .rht_source=sample->rht_source,
        .gas_valid=sample->gas_valid, .heater_stable=sample->heater_stable,
    };
    if (boot == clock->boot_id && clock->revision) {
        int64_t resolved = (int64_t)clock->unix_anchor_s + sample->uptime_s - clock->uptime_anchor_s;
        record.unix_s = resolved >= (int64_t)ENV_HISTORY_UTC_MINIMUM && resolved <= (int64_t)ENV_HISTORY_UTC_MAXIMUM ?
            (uint64_t)resolved : 0;
    }
    return record;
}

static void prepare_page(const struct env_history_control *command, uint32_t request_generation)
{
    int64_t prepare_started_ms = k_uptime_get();
    k_mutex_lock(&history_lock, K_FOREVER);
    struct bt_conn *update_conn = generation == request_generation && owner && !parameters_requested ?
        bt_conn_ref(owner) : NULL;
    if (update_conn) { parameters_requested = true; }
    k_mutex_unlock(&history_lock);
    if (update_conn) {
        const struct bt_le_conn_param parameters = {
            .interval_min=CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
            .interval_max=CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
            .latency=CONFIG_BT_PERIPHERAL_PREF_LATENCY,
            .timeout=CONFIG_BT_PERIPHERAL_PREF_TIMEOUT,
        };
        int parameter_error = bt_conn_le_param_update(update_conn, &parameters);
        printk("CARD HISTORY: parameters requested min=%u max=%u latency=%u timeout=%u error=%d\n",
               parameters.interval_min, parameters.interval_max, parameters.latency,
               parameters.timeout, parameter_error);
        bt_conn_unref(update_conn);
    }
    struct card_sample samples[ENV_HISTORY_MAX_RECORDS];
    struct env_history_record records[ENV_HISTORY_MAX_RECORDS];
    struct sd_logger_history_page found = {0};
    struct card_ble_clock clock; card_ble_clock_snapshot(&clock);
    int error = sd_logger_history_read((struct sd_logger_cursor){command->cursor.boot_id, command->cursor.sequence},
                                       samples, ARRAY_SIZE(samples), &found);
    struct env_history_page metadata = {.request_id=command->request_id, .boot_id=found.boot_id,
        .clock_revision=found.boot_id==clock.boot_id?clock.revision:0,
        .flags=(found.more?ENV_HISTORY_FLAG_MORE:0)|(found.gap?ENV_HISTORY_FLAG_GAP:0)};
    for (size_t i = 0; !error && i < found.count; ++i) {
        records[i] = history_record(&samples[i], found.boot_id, &clock);
        if (i && records[i].sequence != records[i-1].sequence + 1U) { metadata.flags |= ENV_HISTORY_FLAG_GAP; }
    }
    k_mutex_lock(&history_lock, K_FOREVER);
    if (generation != request_generation || !owner) {
        worker_busy = false;
        k_mutex_unlock(&history_lock); return;
    }
    if (k_uptime_get() >= lease_deadline) { error = -ETIMEDOUT; }
    int encoded = 0;
    if (!error && found.count) {
        encoded = env_history_encode_page(&metadata, records, found.count, page_bytes, sizeof(page_bytes));
        if (encoded < 0) { error = encoded; }
    }
    status.earliest = (struct env_history_cursor){found.earliest.boot_id,found.earliest.sequence};
    status.latest = (struct env_history_cursor){found.latest.boot_id,found.latest.sequence};
    status.flags = metadata.flags; status.error = error;
    status.state = error ? ENV_HISTORY_STATE_ERROR : found.count ? ENV_HISTORY_STATE_READY : ENV_HISTORY_STATE_COMPLETE;
    clear_page_locked();
    if (!error && found.count) {
        page_boot_id = found.boot_id;
        page_length = (size_t)encoded;
        status.page_record_count = (uint16_t)found.count;
        status.page_size = (uint16_t)encoded;
        status.page_last_sequence = samples[found.count-1].sequence;
    }
    worker_busy = false; hint_pending = true;
    bool complete = error || !found.count;
    if (!complete) {
        int lease_error = set_deadline_locked(owner, 120000);
        if (lease_error) {
            status.state = ENV_HISTORY_STATE_ERROR; status.error = error = lease_error;
            clear_page_locked(); complete = true;
        }
    }
    int64_t prepare_duration_ms = k_uptime_get() - prepare_started_ms;
    k_mutex_unlock(&history_lock);
    notify_status();
    if (complete) {
        k_mutex_lock(&history_lock, K_FOREVER);
        if (generation == request_generation) { release_owner_locked(); }
        k_mutex_unlock(&history_lock);
    }
    printk("CARD HISTORY: request=%u rows=%u flags=%u error=%d duration_ms=%lld; measurements retained\n",
           command->request_id, (unsigned int)found.count, metadata.flags, error,
           (long long)prepare_duration_ms);
}

static void history_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&history_requested, K_FOREVER);
        k_mutex_lock(&history_lock, K_FOREVER);
        bool pull = pull_pending; pull_pending = false;
        struct env_history_control command = pending_control;
        uint32_t request_generation = generation;
        k_mutex_unlock(&history_lock);
        if (pull) { prepare_page(&command, request_generation); }
        else { notify_status(); }
    }
}

static void history_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(reason);
    sd_logger_history_reset();
    k_mutex_lock(&history_lock, K_FOREVER);
    if (owner == conn || !owner) {
        ++generation; status.state = ENV_HISTORY_STATE_IDLE; status.error = 0;
        if (pull_pending) { pull_pending = false; worker_busy = false; }
        clear_page_locked(); release_owner_locked();
    }
    k_mutex_unlock(&history_lock);
}
BT_CONN_CB_DEFINE(card_history_callbacks) = {.disconnected=history_disconnected};

int card_history_init(void)
{
    k_mutex_lock(&history_lock, K_FOREVER);
    if (initialized) { k_mutex_unlock(&history_lock); return -EALREADY; }
    initialized = true; status.state = ENV_HISTORY_STATE_IDLE;
    k_thread_create(&history_thread, history_stack, K_THREAD_STACK_SIZEOF(history_stack),
                    history_worker, NULL, NULL, NULL, 10, 0, K_NO_WAIT);
    k_mutex_unlock(&history_lock); return 0;
}
