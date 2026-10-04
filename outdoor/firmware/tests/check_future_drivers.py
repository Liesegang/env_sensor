"""Exercise unconnected drivers with a mock bus, compiling their real C sources."""

import ctypes as ct
import errno
from pathlib import Path
import subprocess
import tempfile
import unittest

app = Path(__file__).resolve().parents[1]
temporary = tempfile.TemporaryDirectory(prefix="outdoor-future-drivers-")
root = Path(temporary.name)

headers = {
    "zephyr/drivers/i2c.h": r"""#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct device { bool ready; };
struct i2c_dt_spec { const struct device *bus; uint16_t addr; };
extern struct device mock_bus;
#define DT_NODELABEL(name) name
#define ADDRESS_sfa40 0x5d
#define ADDRESS_as3935 0x03
#define MOCK_ADDRESS_(node) ADDRESS_ ## node
#define MOCK_ADDRESS(node) MOCK_ADDRESS_(node)
#define I2C_DT_SPEC_GET(node) { .bus = &mock_bus, .addr = MOCK_ADDRESS(node) }
#define DT_PROP(node, prop) PROPERTY_ ## prop
#define PROPERTY_indoor 0
#define PROPERTY_noise_floor_level 2
#define PROPERTY_watchdog_threshold 2
#define PROPERTY_minimum_lightnings 1
#define PROPERTY_spike_rejection 2
#define PROPERTY_mask_disturbers 0
#define DT_NODE_HAS_PROP(node, prop) 0
bool i2c_is_ready_dt(const struct i2c_dt_spec *bus);
int i2c_write_dt(const struct i2c_dt_spec *bus, const uint8_t *data, size_t size);
int i2c_read_dt(const struct i2c_dt_spec *bus, uint8_t *data, size_t size);
int i2c_reg_read_byte_dt(const struct i2c_dt_spec *bus, uint8_t reg, uint8_t *value);
int i2c_reg_write_byte_dt(const struct i2c_dt_spec *bus, uint8_t reg, uint8_t value);
int i2c_burst_read_dt(const struct i2c_dt_spec *bus, uint8_t reg, uint8_t *data, size_t size);
""",
    "zephyr/kernel.h": r"""#pragma once
#include <stdint.h>
#include <stddef.h>
int k_msleep(int32_t duration);
int64_t k_uptime_get(void);
""",
    "zephyr/sys/byteorder.h": r"""#pragma once
#include <stdint.h>
static inline void sys_put_be16(uint16_t value, uint8_t *bytes) {
    bytes[0] = value >> 8; bytes[1] = value;
}
""",
}
for name, content in headers.items():
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)

mock = root / "mock.c"
mock.write_text(r"""#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <sensor_codec.h>
#include <errno.h>
#include <string.h>

struct device mock_bus = { true };
static int64_t time_ms;
static uint8_t registers[64];
static uint16_t measurement[4];
static uint16_t commands[32];
static uint8_t write_regs[64], write_values[64];
static unsigned int command_count, write_count;
static uint16_t command;
static int failure;
static bool corrupt;

void mock_reset(void) {
    memset(registers, 0, sizeof(registers));
    registers[0] = 0x24; registers[1] = 0x22; registers[2] = 0xc2;
    registers[8] = 0x17; registers[0x3a] = 0x80; registers[0x3b] = 0x80;
    measurement[0] = 123; measurement[1] = 32768; measurement[2] = 32768; measurement[3] = 0;
    time_ms = 0; command_count = 0; write_count = 0; command = 0;
    failure = 0; corrupt = false; mock_bus.ready = true;
}
void mock_advance(uint32_t duration) { time_ms += duration; }
void mock_fail(int err) { failure = err; }
void mock_corrupt(void) { corrupt = true; }
void mock_word(unsigned int index, uint16_t value) { measurement[index] = value; }
void mock_reg(uint8_t reg, uint8_t value) { registers[reg] = value; }
unsigned int mock_command_count(void) { return command_count; }
unsigned int mock_command(unsigned int index) { return commands[index]; }
unsigned int mock_write_count(void) { return write_count; }
unsigned int mock_write_reg(unsigned int index) { return write_regs[index]; }
unsigned int mock_write_value(unsigned int index) { return write_values[index]; }
unsigned int mock_get_reg(uint8_t reg) { return registers[reg]; }
int k_msleep(int32_t duration) { time_ms += duration; return 0; }
int64_t k_uptime_get(void) { return time_ms; }
bool i2c_is_ready_dt(const struct i2c_dt_spec *bus) { return bus->bus->ready; }
static int take_failure(void) { int err = failure; failure = 0; return err; }

int i2c_write_dt(const struct i2c_dt_spec *bus, const uint8_t *data, size_t size) {
    int err = take_failure(); if (err) return err;
    if (bus->addr != 0x5d || size != 2) return -EINVAL;
    command = ((uint16_t)data[0] << 8) | data[1];
    commands[command_count++] = command;
    return 0;
}
int i2c_read_dt(const struct i2c_dt_spec *bus, uint8_t *data, size_t size) {
    int err = take_failure(); if (err) return err;
    if (bus->addr != 0x5d) return -EINVAL;
    uint16_t words[4];
    size_t count;
    switch (command) {
    case 0x03ff: words[0] = 0x1234; words[1] = 0x5678; count = 2; break;
    case 0x02ce: words[0] = 0x1122; words[1] = 0x3344; words[2] = 0x5566; count = 3; break;
    case 0xc0eb: memcpy(words, measurement, sizeof(words)); count = 4; break;
    default: return -EIO;
    }
    if (size != count * 3) return -EMSGSIZE;
    for (size_t i = 0; i < count; i++) {
        data[3*i] = words[i] >> 8; data[3*i+1] = words[i];
        data[3*i+2] = sensirion_crc8(&data[3*i], 2);
    }
    if (corrupt) { data[size-1] ^= 1; corrupt = false; }
    return 0;
}
int i2c_reg_read_byte_dt(const struct i2c_dt_spec *bus, uint8_t reg, uint8_t *value) {
    int err = take_failure(); if (err) return err;
    if (bus->addr != 0x03 || reg >= sizeof(registers)) return -EINVAL;
    *value = registers[reg];
    if (reg == 3) registers[reg] &= 0xf0;
    return 0;
}
int i2c_reg_write_byte_dt(const struct i2c_dt_spec *bus, uint8_t reg, uint8_t value) {
    int err = take_failure(); if (err) return err;
    if (bus->addr != 0x03 || reg >= sizeof(registers)) return -EINVAL;
    write_regs[write_count] = reg; write_values[write_count++] = value;
    if (reg != 0x3d) registers[reg] = value;
    return 0;
}
int i2c_burst_read_dt(const struct i2c_dt_spec *bus, uint8_t reg, uint8_t *data, size_t size) {
    int err = take_failure(); if (err) return err;
    if (bus->addr != 0x03 || reg + size > sizeof(registers)) return -EINVAL;
    memcpy(data, &registers[reg], size);
    if (reg <= 3 && reg + size > 3) registers[3] &= 0xf0;
    return 0;
}
""")

library_path = root / "future_drivers.dylib"
command = ["/usr/bin/cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
           "-I", str(root)]
for driver in ("common", "sfa40", "as3935"):
    command += ["-I", str(app / "driver" / driver / "include")]
for source in ("common/sensor_bus.c", "common/sensor_codec.c", "sfa40/sfa40.c",
               "sfa40/sfa40_decode.c", "as3935/as3935.c", "as3935/as3935_decode.c"):
    command.append(str(app / "driver" / source))
subprocess.run(command + [str(mock), "-o", str(library_path)], check=True)
lib = ct.CDLL(str(library_path))


class SfaSample(ct.Structure):
    _fields_ = [("serial_number", ct.c_uint64), ("product_id", ct.c_uint32),
                ("hcho_millippb", ct.c_int32), ("humidity_mpercent", ct.c_int32),
                ("temperature_mc", ct.c_int32), ("raw_hcho", ct.c_uint16),
                ("raw_humidity", ct.c_uint16), ("raw_temperature", ct.c_uint16),
                ("status", ct.c_uint8), ("ready", ct.c_bool),
                ("within_specification", ct.c_bool)]


class AsSample(ct.Structure):
    _fields_ = [("energy", ct.c_uint32), ("distance_km", ct.c_int16)]
    _fields_ += [(name, ct.c_uint8) for name in (
        "interrupt_reason", "distance_code", "noise_floor", "watchdog_threshold",
        "spike_rejection", "minimum_lightnings", "tuning_capacitor",
        "trco_calibration", "srco_calibration")]
    _fields_ += [(name, ct.c_bool) for name in (
        "lightning", "noise_high", "disturber", "storm_overhead", "out_of_range",
        "distance_valid", "indoor", "powered_down", "disturbers_masked")]


lib.sfa40_read.argtypes = [ct.POINTER(SfaSample)]
lib.as3935_read.argtypes = [ct.POINTER(AsSample)]
lib.mock_reg.argtypes = [ct.c_uint8, ct.c_uint8]
lib.mock_word.argtypes = [ct.c_uint, ct.c_uint16]
lib.mock_fail.argtypes = [ct.c_int]
lib.mock_advance.argtypes = [ct.c_uint32]


class FutureDriverTests(unittest.TestCase):
    def setUp(self):
        lib.sfa40_stop()
        lib.as3935_stop()
        lib.mock_reset()

    def start_sfa(self):
        self.assertEqual(lib.sfa40_init(), 0)
        lib.mock_advance(500)

    def test_sfa_initialization_commands_identity_and_first_sample_delay(self):
        self.assertEqual(lib.sfa40_init(), 0)
        self.assertEqual([lib.mock_command(i) for i in range(lib.mock_command_count())],
                         [0x50D2, 0x03FF, 0x02CE, 0x00AC])
        sample = SfaSample()
        self.assertEqual(lib.sfa40_read(ct.byref(sample)), -errno.EAGAIN)
        lib.mock_advance(500)
        self.assertEqual(lib.sfa40_read(ct.byref(sample)), 0)
        self.assertEqual(sample.serial_number, 0x112233445566)
        self.assertEqual(sample.product_id, 0x12345678)
        self.assertEqual(lib.mock_command(lib.mock_command_count()-1), 0xC0EB)

    def test_sfa_units_and_warmup_status_byte(self):
        self.start_sfa()
        for status, ready, within in ((3, False, False), (2, True, False), (0, True, True)):
            lib.mock_word(3, status << 8)
            sample = SfaSample()
            self.assertEqual(lib.sfa40_read(ct.byref(sample)), 0)
            self.assertEqual((sample.hcho_millippb, sample.humidity_mpercent,
                              sample.temperature_mc), (12300, 56500, 42501))
            self.assertEqual((sample.status, sample.ready, sample.within_specification),
                             (status, ready, within))

    def test_sfa_crc_failure_leaves_previous_output_unchanged(self):
        self.start_sfa()
        sample = SfaSample(hcho_millippb=9876, serial_number=123)
        before = bytes(sample)
        lib.mock_corrupt()
        self.assertEqual(lib.sfa40_read(ct.byref(sample)), -errno.EBADMSG)
        self.assertEqual(bytes(sample), before)

    def test_sfa_reserved_byte_rejected_and_limits_clamped(self):
        self.start_sfa()
        sample = SfaSample()
        lib.mock_word(3, 1)
        self.assertEqual(lib.sfa40_read(ct.byref(sample)), -errno.EBADMSG)
        lib.mock_word(3, 0)
        for raw, rh, temp in ((0, 0, -45000), (65535, 100000, 130000)):
            lib.mock_word(1, raw)
            lib.mock_word(2, raw)
            self.assertEqual(lib.sfa40_read(ct.byref(sample)), 0)
            self.assertEqual((sample.humidity_mpercent, sample.temperature_mc), (rh, temp))

    def test_unconnected_sensors_propagate_bus_failure(self):
        for initializer in (lib.sfa40_init, lib.as3935_init):
            lib.mock_fail(-errno.ENXIO)
            self.assertEqual(initializer(), -errno.ENXIO)
        self.assertEqual(lib.sfa40_read(ct.byref(SfaSample())), -errno.EACCES)
        self.assertEqual(lib.as3935_read(ct.byref(AsSample())), -errno.EACCES)

    def test_stopped_drivers_and_null_outputs(self):
        self.start_sfa()
        self.assertEqual(lib.sfa40_stop(), 0)
        self.assertEqual(lib.sfa40_read(ct.byref(SfaSample())), -errno.EACCES)
        self.assertEqual(lib.as3935_init(), 0)
        self.assertEqual(lib.as3935_stop(), 0)
        self.assertEqual(lib.as3935_read(ct.byref(AsSample())), -errno.EACCES)
        self.assertEqual(lib.sfa40_read(None), -errno.EINVAL)
        self.assertEqual(lib.as3935_read(None), -errno.EINVAL)

    def test_as_outdoor_configuration_preserves_reserved_bits_and_antenna_tuning(self):
        self.assertEqual(lib.as3935_init(), 0)
        self.assertEqual(lib.mock_get_reg(0), 0x1C)
        self.assertEqual(lib.mock_get_reg(2), 0xC2)
        self.assertEqual(lib.mock_get_reg(8), 0x17)
        writes = [(lib.mock_write_reg(i), lib.mock_write_value(i))
                  for i in range(lib.mock_write_count())]
        self.assertIn((0x3D, 0x96), writes)
        self.assertIn((0x08, 0x57), writes)
        self.assertEqual(writes[-1], (0x08, 0x17))

    def test_as_calibration_failure_prevents_ready_state(self):
        lib.mock_reg(0x3A, 0xC0)
        self.assertEqual(lib.as3935_init(), -errno.EIO)
        self.assertEqual(lib.as3935_read(ct.byref(AsSample())), -errno.EACCES)

    def test_as_energy_distance_and_irq_acknowledgement(self):
        self.assertEqual(lib.as3935_init(), 0)
        for reg, value in ((3, 0xC8), (4, 0x56), (5, 0x34), (6, 0xE2), (7, 0x8E)):
            lib.mock_reg(reg, value)
        sample = AsSample()
        self.assertEqual(lib.as3935_read(ct.byref(sample)), 0)
        self.assertEqual((sample.energy, sample.distance_km, sample.interrupt_reason),
                         (0x23456, 14, 8))
        self.assertTrue(sample.lightning)
        self.assertTrue(sample.distance_valid)
        self.assertEqual(lib.as3935_read(ct.byref(sample)), 0)
        self.assertFalse(sample.lightning)

    def test_as_special_distance_codes_and_noise_flags(self):
        self.assertEqual(lib.as3935_init(), 0)
        for distance, valid, overhead, outside in (
            (1, True, True, False), (63, False, False, True), (0, False, False, False),
            (2, False, False, False)):
            lib.mock_reg(3, 0x25)
            lib.mock_reg(7, distance)
            sample = AsSample()
            self.assertEqual(lib.as3935_read(ct.byref(sample)), 0)
            self.assertEqual(sample.distance_km, -1)
            self.assertEqual((sample.distance_valid, sample.storm_overhead, sample.out_of_range),
                             (valid, overhead, outside))
            self.assertTrue(sample.noise_high and sample.disturber and sample.disturbers_masked)
            self.assertFalse(sample.lightning)

    def test_as_clear_statistics_toggles_only_its_control_bit(self):
        self.assertEqual(lib.as3935_init(), 0)
        start = lib.mock_write_count()
        self.assertEqual(lib.as3935_clear_statistics(), 0)
        self.assertEqual([(lib.mock_write_reg(i), lib.mock_write_value(i))
                          for i in range(start, lib.mock_write_count())],
                         [(2, 0xC2), (2, 0x82), (2, 0xC2)])


if __name__ == "__main__":
    try:
        unittest.main(verbosity=2)
    finally:
        temporary.cleanup()
