"""Exercise the real STCC4 driver with timed NACKs and corrupt I2C responses."""

from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="outdoor-stcc4-") as temporary:
    root = Path(temporary)
    headers = {
        "zephyr/drivers/i2c.h": r"""#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct i2c_dt_spec { int unused; };
#define DT_NODELABEL(node) node
#define I2C_DT_SPEC_GET(node) { 0 }
bool i2c_is_ready_dt(const struct i2c_dt_spec *bus);
int i2c_write_dt(const struct i2c_dt_spec *bus, const uint8_t *data, size_t size);
int i2c_read_dt(const struct i2c_dt_spec *bus, uint8_t *data, size_t size);
""",
        "zephyr/kernel.h": r"""#pragma once
#include <stdint.h>
int k_msleep(int32_t duration);
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
    test = root / "test.c"
    test.write_text(r"""#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stcc4.h>
#include <sensor_codec.h>
#include <zephyr/drivers/i2c.h>

static uint16_t command;
static unsigned int reads, writes, sleeps_ms, nacks;
static int command_error, read_error;
static bool corrupt;

bool i2c_is_ready_dt(const struct i2c_dt_spec *bus) { (void)bus; return true; }
int k_msleep(int32_t duration) { sleeps_ms += duration; return 0; }
int i2c_write_dt(const struct i2c_dt_spec *bus, const uint8_t *data, size_t size) {
    (void)bus;
    writes++;
    if (command_error) return command_error;
    if (size == 1) return 0;
    assert(size == 2);
    command = ((uint16_t)data[0] << 8) | data[1];
    return 0;
}
int i2c_read_dt(const struct i2c_dt_spec *bus, uint8_t *data, size_t size) {
    (void)bus;
    reads++;
    if (nacks) { nacks--; return -EIO; }
    if (read_error) return read_error;
    const uint16_t identity[] = {0x0901, 0x018a, 0x2471, 0x6d16, 0x3cba, 0x7cc2};
    const uint16_t measurement[] = {402, 25606, 35608, 0};
    const uint16_t *words = command == 0x365b ? identity : measurement;
    assert(command == 0x365b || command == 0xec05);
    assert(size == (command == 0x365b ? 18 : 12));
    for (size_t i = 0; i < size / 3; i++) {
        data[i*3] = words[i] >> 8;
        data[i*3+1] = words[i];
        data[i*3+2] = sensirion_crc8(&data[i*3], 2);
    }
    if (corrupt) data[size-1] ^= 1;
    return 0;
}
static void reset_counters(void) {
    reads = writes = sleeps_ms = nacks = 0;
    command_error = read_error = 0;
    corrupt = false;
}
static void check_unchanged(int error, unsigned int expected_reads,
                            unsigned int expected_sleeps) {
    struct stcc4_sample sample;
    memset(&sample, 0x5a, sizeof(sample));
    unsigned char before[sizeof(sample)];
    memcpy(before, &sample, sizeof(sample));
    assert(stcc4_read(&sample) == error);
    assert(memcmp(before, &sample, sizeof(sample)) == 0);
    assert(reads == expected_reads && sleeps_ms == expected_sleeps);
    assert(writes == 1);
}
int main(void) {
    assert(stcc4_read(NULL) == -EINVAL);
    struct stcc4_sample sample;
    assert(stcc4_read(&sample) == -EACCES);
    assert(stcc4_init() == 0);

    reset_counters();
    assert(stcc4_read(&sample) == 0);
    assert(reads == 1 && writes == 1 && sleeps_ms == 1);
    assert(sample.co2_ppm == 402 && sample.raw_temperature == 25606);
    assert(sample.raw_humidity == 35608 && sample.status == 0);
    assert(sample.serial_number == UINT64_C(0x24716d163cba7cc2));
    assert(sample.product_id == 0x0901018a);

    reset_counters();
    nacks = 1;
    assert(stcc4_read(&sample) == 0);
    assert(reads == 2 && writes == 1 && sleeps_ms == 151);
    assert(sample.co2_ppm == 402);

    reset_counters();
    nacks = 2;
    check_unchanged(-EAGAIN, 2, 151);

    reset_counters();
    command_error = -EIO;
    check_unchanged(-EIO, 0, 0);

    reset_counters();
    corrupt = true;
    check_unchanged(-EBADMSG, 1, 1);

    reset_counters();
    read_error = -ETIMEDOUT;
    check_unchanged(-ETIMEDOUT, 1, 1);

    assert(stcc4_stop() == 0);
    assert(stcc4_read(&sample) == -EACCES);
    puts("STCC4: ready, timed NACK retry, bounded missing data, command errors, CRC and timeout OK (6 cases)");
    return 0;
}
""")
    executable = root / "test"
    command = ["/usr/bin/cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root)]
    for driver in ("common", "stcc4", "sht45"):
        command += ["-I", str(app / "driver" / driver / "include")]
    for source in ("common/sensor_bus.c", "common/sensor_codec.c", "stcc4/stcc4.c", "sht45/sht45.c"):
        command.append(str(app / "driver" / source))
    subprocess.run(command + [str(test), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
