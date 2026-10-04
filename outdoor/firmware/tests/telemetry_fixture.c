/* Cross-language fixture: real C encoder output is decoded by TypeScript tests. */
#include <telemetry.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hex(const uint8_t *bytes, size_t length)
{
	putchar('"');
	for (size_t i = 0; i < length; i++) { printf("%02x", bytes[i]); }
	putchar('"');
}

static void fragments(const uint8_t *frame, size_t length, size_t capacity)
{
	uint8_t bytes[244];
	putchar('[');
	for (size_t offset = 0; offset < length;) {
		size_t count = telemetry_fragment(frame, length, offset, bytes, capacity);
		assert(count > 12 && count <= capacity);
		if (offset) { putchar(','); }
		hex(bytes, count);
		offset += count - 12;
	}
	putchar(']');
}

int main(void)
{
	struct telemetry_sample sample = {
		.sequence = 42, .uptime_ms = 12345678901234ULL, .enabled_mask = 0x7f,
		.opt4001.value = { .millilux = 65944, .mantissa = 123456, .device_id = 0x0121, .exponent = 4, .counter = 3 },
		.bme690.value = { .temperature_mc = -1234, .pressure_pa = 101803, .humidity_mpercent = 54880,
			.gas_resistance_ohm = 8100, .chip_id = 0x61, .variant_id = 1, .status = 0xb0, .gas_valid = true, .heater_stable = true },
		.sgp41.value = { .serial_number = 0x112233445566ULL, .raw_voc = 29429, .raw_nox = 17492,
			.self_test = 0xd400, .temperature_ticks = 25000, .humidity_ticks = 32768, .nox_valid = true,
			.voc_index = 100, .nox_index = 1, .voc_index_valid = true, .nox_index_valid = true },
		.stcc4.value = { .serial_number = 0xfedcba9876543210ULL, .product_id = 0x12345678,
			.temperature_mc = 23274, .humidity_mpercent = 54908, .co2_ppm = 391, .raw_temperature = 25568, .raw_humidity = 32000, .status = 0x8000 },
		.bmv080.value = { .pm1_mass_milliug_m3 = 1200, .pm2_5_mass_milliug_m3 = 2500, .pm10_mass_milliug_m3 = 10000,
			.pm1_number_milliparticles_cm3 = 123, .pm2_5_number_milliparticles_cm3 = 234, .pm10_number_milliparticles_cm3 = 345,
			.runtime_ms = 9876543210ULL, .sequence = 40, .age_ms = 100, .fresh = true },
		.as3935.value = { .energy = 0x123456, .distance_km = 14, .distance_code = 14, .interrupt_reason = 8,
			.noise_floor = 2, .watchdog_threshold = 2, .spike_rejection = 2, .minimum_lightnings = 1,
			.trco_calibration = 0x80, .srco_calibration = 0x80, .lightning = true, .distance_valid = true },
		.sfa40.value = { .serial_number = 0x112233445566ULL, .product_id = 0x12345678, .hcho_millippb = 12300,
			.humidity_mpercent = 56500, .temperature_mc = 42501, .raw_hcho = 123, .raw_humidity = 32768,
			.raw_temperature = 32768, .status = 2, .ready = true },
	};
	uint8_t frame[TELEMETRY_MAX_SIZE];
	size_t size = telemetry_encode(&sample, frame, sizeof(frame));
	assert(size > 24 && size < 512 && frame[6] == 0x7f && frame[8] == 0x7f);
	assert(frame[12] == 42 && frame[13] == 0);
	uint8_t guarded[TELEMETRY_MAX_SIZE + 1];
	memset(guarded, 0xa5, sizeof(guarded));
	assert(telemetry_encode(&sample, guarded, size - 1) == 0);
	assert(guarded[size - 1] == 0xa5);
	assert(telemetry_encode(NULL, frame, sizeof(frame)) == 0);
	assert(telemetry_encode(&sample, NULL, sizeof(frame)) == 0);
	assert(telemetry_fragment(frame, size, size, guarded, 20) == 0);
	assert(telemetry_fragment(frame, size, 0, guarded, 12) == 0);
	printf("{\"all\":"); hex(frame, size);
	printf(",\"fragments20\":"); fragments(frame, size, 20);
	printf(",\"fragments244\":"); fragments(frame, size, 244);
	sample.bme690.error = -5; sample.sgp41.error = -11;
	size = telemetry_encode(&sample, frame, sizeof(frame));
	assert(frame[8] == 0x79);
	printf(",\"failed\":"); hex(frame, size);
	sample.bme690.error = 0; sample.sgp41.error = 0; sample.enabled_mask = 0x0f;
	size = telemetry_encode(&sample, frame, sizeof(frame));
	printf(",\"current\":"); hex(frame, size);
	sample.enabled_mask = 0;
	size = telemetry_encode(&sample, frame, sizeof(frame));
	assert(size == 24);
	printf(",\"empty\":"); hex(frame, size);
	sample.enabled_mask = 0x80;
	assert(telemetry_encode(&sample, frame, sizeof(frame)) == 0);
	sample.enabled_mask = 1; sample.opt4001.error = 5;
	assert(telemetry_encode(&sample, frame, sizeof(frame)) == 0);
	puts("}");
	return 0;
}
