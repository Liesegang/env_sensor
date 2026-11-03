#ifndef CARD_SAMPLE_H_
#define CARD_SAMPLE_H_

#include <stdint.h>

enum card_sample_valid {
	CARD_VALID_TEMPERATURE = 1U << 0,
	CARD_VALID_HUMIDITY = 1U << 1,
	CARD_VALID_PRESSURE = 1U << 2,
	CARD_VALID_CO2 = 1U << 3,
	CARD_VALID_LUX = 1U << 4,
	CARD_VALID_BME_TEMPERATURE = 1U << 5,
	CARD_VALID_BME_HUMIDITY = 1U << 6,
	CARD_VALID_BME_GAS = 1U << 7,
	CARD_VALID_VOC_RAW = 1U << 8,
	CARD_VALID_NOX_RAW = 1U << 9,
	CARD_VALID_VOC_INDEX = 1U << 10,
	CARD_VALID_NOX_INDEX = 1U << 11,
	CARD_VALID_HCHO = 1U << 12,
};

enum card_rht_source {
	CARD_RHT_NONE,
	CARD_RHT_SHT45,
	CARD_RHT_BME690,
};

/* One measured sample. Fields without a valid bit are missing, not zero.
 * unix_s is UTC seconds supplied by a trusted receiver; zero means unknown.
 * uptime_s always remains seconds since this MCU boot.
 */
struct card_sample {
	uint32_t sequence;
	uint32_t uptime_s;
	uint64_t unix_s;
	int32_t temperature_mc;
	uint32_t humidity_mpercent;
	int32_t hcho_millippb;
	int32_t bme_temperature_mc;
	uint32_t bme_humidity_mpercent;
	uint32_t pressure_pa;
	uint32_t gas_resistance_ohm;
	uint32_t millilux;
	int16_t co2_ppm;
	uint16_t raw_voc;
	uint16_t raw_nox;
	uint16_t voc_index;
	uint16_t nox_index;
	uint16_t stcc_status;
	uint16_t stcc_self_test;
	uint16_t valid;
	uint8_t rht_source;
	uint8_t gas_valid;
	uint8_t heater_stable;
	uint8_t reserved;
};

#endif
