#ifndef OUTDOOR_AS3935_H_
#define OUTDOOR_AS3935_H_

#include <stdbool.h>
#include <stdint.h>

struct as3935_sample {
	uint32_t energy;
	int16_t distance_km; /* -1 when no numeric estimate; see the flags/code. */
	uint8_t interrupt_reason;
	uint8_t distance_code;
	uint8_t noise_floor;
	uint8_t watchdog_threshold;
	uint8_t spike_rejection;
	uint8_t minimum_lightnings;
	uint8_t tuning_capacitor;
	uint8_t trco_calibration;
	uint8_t srco_calibration;
	bool lightning;
	bool noise_high;
	bool disturber;
	bool storm_overhead;
	bool out_of_range;
	bool distance_valid;
	bool indoor;
	bool powered_down;
	bool disturbers_masked;
};

int as3935_init(void);
/* Thread context only. Reading register 0x03 acknowledges/clears its IRQ. */
int as3935_read(struct as3935_sample *sample);
int as3935_calibrate(void);
int as3935_clear_statistics(void);
int as3935_stop(void);
int as3935_decode_registers(const uint8_t registers[9], uint8_t trco, uint8_t srco,
			    struct as3935_sample *sample);

#endif
