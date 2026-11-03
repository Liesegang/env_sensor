#ifndef TEST_ENV_TELEMETRY_BLE_H
#define TEST_ENV_TELEMETRY_BLE_H
enum env_telemetry_pairing_event { ENV_TELEMETRY_PAIRING_OPENED, ENV_TELEMETRY_PAIRING_PASSKEY, ENV_TELEMETRY_PAIRING_CLOSED };
#endif
#include <stdint.h>
void env_telemetry_ble_pairing_display_ready(uint32_t,int);
