#include <as3935.h>

#include <errno.h>
#include <stddef.h>

int as3935_decode_registers(const uint8_t registers[9], uint8_t trco, uint8_t srco,
			    struct as3935_sample *sample)
{
	if (registers == NULL || sample == NULL) {
		return -EINVAL;
	}
	uint8_t distance = registers[7] & 0x3f;
	bool numeric_distance;

	switch (distance) {
	case 5: case 6: case 8: case 10: case 12: case 14: case 17: case 20:
	case 24: case 27: case 31: case 34: case 37: case 40:
		numeric_distance = true;
		break;
	default:
		numeric_distance = false;
		break;
	}
	static const uint8_t minimum_events[] = {1, 5, 9, 16};

	*sample = (struct as3935_sample){
		.energy = ((uint32_t)(registers[6] & 0x1f) << 16) |
			  ((uint32_t)registers[5] << 8) | registers[4],
		.distance_km = numeric_distance ? distance : -1,
		.interrupt_reason = registers[3] & 0x0f,
		.distance_code = distance,
		.noise_floor = (registers[1] >> 4) & 0x07,
		.watchdog_threshold = registers[1] & 0x0f,
		.spike_rejection = registers[2] & 0x0f,
		.minimum_lightnings = minimum_events[(registers[2] >> 4) & 0x03],
		.tuning_capacitor = registers[8] & 0x0f,
		.trco_calibration = trco,
		.srco_calibration = srco,
		.lightning = (registers[3] & 0x08) != 0,
		.noise_high = (registers[3] & 0x01) != 0,
		.disturber = (registers[3] & 0x04) != 0,
		.storm_overhead = distance == 1,
		.out_of_range = distance == 0x3f,
		.distance_valid = numeric_distance || distance == 1,
		.indoor = ((registers[0] >> 1) & 0x1f) == 0x12,
		.powered_down = (registers[0] & 0x01) != 0,
		.disturbers_masked = (registers[3] & 0x20) != 0,
	};
	return 0;
}
