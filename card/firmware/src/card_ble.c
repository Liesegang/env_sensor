/* SPDX-License-Identifier: MIT */
#include "card_ble.h"
#include "card_display.h"
#include <env_telemetry_ble.h>
#include <env_clock.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/random.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

static bool push_held_latch;
static bool push_consumed;
static int64_t push_started_ms = -1;
K_MUTEX_DEFINE(clock_lock);
static struct card_ble_clock clock_state;

static int set_clock_packet(const void *buffer, size_t length, void *context)
{
    ARG_UNUSED(context);
    uint64_t unix_ms = 0;
    int decoded = env_clock_decode_write(buffer, length, &unix_ms);
    if (decoded < 0) { return decoded; }
    k_mutex_lock(&clock_lock, K_FOREVER);
    clock_state.unix_anchor_s = unix_ms / 1000;
    clock_state.uptime_anchor_s = (uint32_t)(k_uptime_get() / 1000);
    if (++clock_state.revision == 0) { ++clock_state.revision; }
    k_mutex_unlock(&clock_lock);
    return 0;
}

uint64_t card_ble_unix_seconds(int64_t uptime_ms)
{
    k_mutex_lock(&clock_lock, K_FOREVER);
    uint64_t result = 0;
    if (clock_state.unix_anchor_s && uptime_ms >= 0) {
        int64_t estimated = (int64_t)clock_state.unix_anchor_s + uptime_ms / 1000 - clock_state.uptime_anchor_s;
        if (estimated >= 1577836800LL && estimated < 4102444800LL) { result = (uint64_t)estimated; }
    }
    k_mutex_unlock(&clock_lock);
    return result;
}

void card_ble_clock_snapshot(struct card_ble_clock *clock)
{
    if (!clock) { return; }
    k_mutex_lock(&clock_lock, K_FOREVER);
    *clock = clock_state;
    k_mutex_unlock(&clock_lock);
}

int card_ble_init(void)
{
    /* Reserved high-bit namespace belongs to CSV files created before BootID
     * support. The cryptographic RNG is available independently of bt_enable.
     */
    k_mutex_lock(&clock_lock, K_FOREVER);
    if (!clock_state.boot_id) {
        uint64_t identity = 0;
        int random_error = sys_csrand_get(&identity, sizeof(identity));
        if (random_error) { k_mutex_unlock(&clock_lock); return random_error; }
        identity &= INT64_MAX;
        if (!identity) { identity = 1; }
        clock_state.boot_id = identity;
    }
    k_mutex_unlock(&clock_lock);
    printk("CARD BLE: startup secure=%u\n", IS_ENABLED(CONFIG_ENV_TELEMETRY_SECURE));
    struct env_telemetry_sample initial = {
        .uptime_ms = k_uptime_get(), .enabled_mask = 0x4f,
        .opt4001.error = -EAGAIN, .bme690.error = -EAGAIN,
        .sgp41.error = -EAGAIN, .stcc4.error = -EAGAIN,
        .sfa40.error = -EAGAIN,
    };
    int err = env_telemetry_ble_publish(&initial);
    if (err) { printk("ERROR: CARD BLE initial telemetry cache=%d\n", err); return err; }
    printk("CARD BLE: initial telemetry cached; initializing transport\n");
    /* 100 ms advertising during the short window; MCU uptime, no RTC sync. */
    const struct env_telemetry_ble_config config = {
        .log_publications = true, .active_window_ms = 5000,
        .advertising_interval_min = 160, .advertising_interval_max = 160,
        .disable_between_windows = true,
        .pairing_window_ms = 180000,
        .pairing_event = card_display_pairing_event,
        .set_clock_packet = set_clock_packet,
    };
    err = env_telemetry_ble_init(&config);
    if (err) { printk("ERROR: CARD BLE transport init=%d\n", err); }
    return err;
}

void card_ble_check_pairing_button(void)
{
    static const struct gpio_dt_spec push =
        GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), swt_gpios);
    int active = gpio_pin_get_dt(&push);
    /* An unreadable input breaks the physical authorization interval too. */
    if (active != 1) { push_held_latch = false; push_started_ms = -1; return; }
    if (push_held_latch) { return; }
    int64_t now = k_uptime_get();
    if (push_started_ms < 0) {
        push_started_ms = now;
        push_consumed = false;
        return;
    }
    if (now - push_started_ms < 3000) { return; }
    push_held_latch = true;
    push_consumed = true;
    int err = env_telemetry_ble_repair();
    printk("BLE SECURITY: physical re-pair requested error=%d\n", err);
}

bool card_ble_pairing_button_consumed(void) { return push_consumed; }

int card_ble_publish(const struct card_sample *sample,
                     const struct opt4001_sample *opt4001,
                     const struct bme690_sample *bme690,
                     const struct sgp41_sample *sgp41,
                     const struct stcc4_sample *stcc4,
                     const struct sfa40_sample *sfa40)
{
    if (!sample) { return -EINVAL; }
    struct env_telemetry_sample frame = {
        .sequence = sample->sequence, .uptime_ms = (uint64_t)sample->uptime_s * 1000,
        .enabled_mask = 0x4f,
        .opt4001.error = -ENODATA, .bme690.error = -ENODATA,
        .sgp41.error = -ENODATA, .stcc4.error = -ENODATA,
        .sfa40.error = -ENODATA,
    };
    if (opt4001) {
        frame.opt4001.error = 0;
        frame.opt4001.value.millilux = opt4001->millilux;
        frame.opt4001.value.mantissa = opt4001->mantissa;
        frame.opt4001.value.device_id = opt4001->device_id;
        frame.opt4001.value.exponent = opt4001->exponent;
        frame.opt4001.value.counter = opt4001->counter;
    }
    if (bme690) {
        frame.bme690.error = 0;
        frame.bme690.value.temperature_mc = bme690->temperature_mc;
        frame.bme690.value.pressure_pa = bme690->pressure_pa;
        frame.bme690.value.humidity_mpercent = bme690->humidity_mpercent;
        frame.bme690.value.gas_resistance_ohm = bme690->gas_resistance_ohm;
        frame.bme690.value.chip_id = bme690->chip_id;
        frame.bme690.value.variant_id = bme690->variant_id;
        frame.bme690.value.status = bme690->status;
        frame.bme690.value.measurement_index = bme690->measurement_index;
        frame.bme690.value.gas_index = bme690->gas_index;
        frame.bme690.value.heater_resistance = bme690->heater_resistance;
        frame.bme690.value.heater_current = bme690->heater_current;
        frame.bme690.value.gas_wait = bme690->gas_wait;
        frame.bme690.value.gas_valid = bme690->gas_valid;
        frame.bme690.value.heater_stable = bme690->heater_stable;
    }
    if (sgp41) {
        frame.sgp41.error = 0;
        frame.sgp41.value.serial_number = sgp41->serial_number;
        frame.sgp41.value.raw_voc = sgp41->raw_voc;
        frame.sgp41.value.raw_nox = sgp41->raw_nox;
        frame.sgp41.value.self_test = sgp41->self_test;
        frame.sgp41.value.temperature_ticks = sgp41->temperature_ticks;
        frame.sgp41.value.humidity_ticks = sgp41->humidity_ticks;
        frame.sgp41.value.nox_valid = sgp41->nox_valid;
        frame.sgp41.value.conditioning = sgp41->conditioning;
        frame.sgp41.value.voc_index = sgp41->voc_index;
        frame.sgp41.value.nox_index = sgp41->nox_index;
        frame.sgp41.value.voc_index_valid = sgp41->voc_index_valid;
        frame.sgp41.value.nox_index_valid = sgp41->nox_index_valid;
    }
    if (stcc4) {
        frame.stcc4.error = 0;
        frame.stcc4.value.serial_number = stcc4->serial_number;
        frame.stcc4.value.product_id = stcc4->product_id;
        frame.stcc4.value.temperature_mc = stcc4->temperature_mc;
        frame.stcc4.value.humidity_mpercent = stcc4->humidity_mpercent;
        frame.stcc4.value.co2_ppm = stcc4->co2_ppm;
        frame.stcc4.value.raw_temperature = stcc4->raw_temperature;
        frame.stcc4.value.raw_humidity = stcc4->raw_humidity;
        frame.stcc4.value.status = stcc4->status;
    }
    if (sfa40) {
        frame.sfa40.error = 0;
        frame.sfa40.value.serial_number = sfa40->serial_number;
        frame.sfa40.value.product_id = sfa40->product_id;
        frame.sfa40.value.hcho_millippb = sfa40->hcho_millippb;
        frame.sfa40.value.humidity_mpercent = sfa40->humidity_mpercent;
        frame.sfa40.value.temperature_mc = sfa40->temperature_mc;
        frame.sfa40.value.raw_hcho = sfa40->raw_hcho;
        frame.sfa40.value.raw_humidity = sfa40->raw_humidity;
        frame.sfa40.value.raw_temperature = sfa40->raw_temperature;
        frame.sfa40.value.status = sfa40->status;
        frame.sfa40.value.ready = sfa40->ready;
        frame.sfa40.value.within_specification = sfa40->within_specification;
    }
    return env_telemetry_ble_publish(&frame);
}

/* The dedicated clock service shares the exact setter and integer-second
 * anchors used by legacy OT and history. It appends after existing services.
 */
#define CLOCK_PERMISSIONS (BT_GATT_PERM_READ_LESC | BT_GATT_PERM_WRITE_LESC | \
    BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN)
#define CLOCK_UUID(n) BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x722acd00U+(n),0xe77f,0x468d,0xa916,0x67e599e7cffdULL))
static struct bt_uuid_128 clock_uuid = CLOCK_UUID(0xb0);
static struct bt_uuid_128 clock_write_uuid = CLOCK_UUID(0xb1);
static struct bt_uuid_128 clock_state_uuid = CLOCK_UUID(0xb2);
K_MUTEX_DEFINE(clock_read_lock);
static uint8_t clock_read_snapshot[32];

static ssize_t write_dedicated_clock(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buffer, uint16_t length, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(attr);
    if (bt_conn_get_security(conn) != BT_SECURITY_L4) { return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION); }
    if (offset || flags) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    int error = env_telemetry_ble_hold_history(conn, 5000);
    if (!error) { error = set_clock_packet(buffer, length, NULL); }
    return error ? BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED) : length;
}

static ssize_t read_dedicated_clock(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buffer, uint16_t length, uint16_t offset)
{
    if (bt_conn_get_security(conn) != BT_SECURITY_L4) { return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION); }
    int error = env_telemetry_ble_hold_history(conn, 5000);
    if (error) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    k_mutex_lock(&clock_read_lock, K_FOREVER);
    if (!offset) {
        struct card_ble_clock clock; card_ble_clock_snapshot(&clock);
        struct env_clock_state state = {
            .flags=clock.revision?ENV_CLOCK_FLAG_KNOWN:0, .boot_id=clock.boot_id, .clock_revision=clock.revision,
            .unix_anchor_ms=clock.unix_anchor_s*1000,
            .uptime_anchor_ms=(uint64_t)clock.uptime_anchor_s*1000,
        };
        error = env_clock_encode_state(&state, clock_read_snapshot, sizeof(clock_read_snapshot));
        if (error >= 0) { error = 0; }
    }
    ssize_t result = error ? BT_GATT_ERR(BT_ATT_ERR_UNLIKELY) :
        bt_gatt_attr_read(conn, attr, buffer, length, offset, clock_read_snapshot, sizeof(clock_read_snapshot));
    k_mutex_unlock(&clock_read_lock); return result;
}

BT_GATT_SERVICE_DEFINE(zzzz_env_clock_service,
    BT_GATT_PRIMARY_SERVICE(&clock_uuid),
    BT_GATT_CHARACTERISTIC(&clock_write_uuid.uuid, BT_GATT_CHRC_WRITE, CLOCK_PERMISSIONS, NULL, write_dedicated_clock, NULL),
    BT_GATT_CHARACTERISTIC(&clock_state_uuid.uuid, BT_GATT_CHRC_READ, CLOCK_PERMISSIONS, read_dedicated_clock, NULL, NULL)
);
