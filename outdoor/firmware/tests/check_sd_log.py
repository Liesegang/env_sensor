"""Execute the clock and schema-driven CSV formatter with captured wire fixtures."""
import csv
import ctypes as ct
import errno
import io
import json
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
subprocess.run(['python3', str(app.parent / 'protocol/generate_csv.py'), '--check'], check=True)
with tempfile.TemporaryDirectory(prefix='outdoor-sd-') as directory:
    tmp = Path(directory)
    (tmp / 'zephyr').mkdir()
    (tmp / 'zephyr/kernel.h').write_text('''#include <stdint.h>
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static inline int k_spin_lock(struct k_spinlock *lock) { (void)lock; return 0; }
static inline void k_spin_unlock(struct k_spinlock *lock, int key) { (void)lock; (void)key; }
extern int64_t mock_uptime;
static inline int64_t k_uptime_get(void) { return mock_uptime; }
''')
    (tmp / 'mock.c').write_text('#include <stdint.h>\nint64_t mock_uptime;\n')
    library = tmp / 'clock_csv.so'
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-I', str(tmp), '-I', str(app / 'logging/include'), str(app / 'logging/outdoor_clock.c'),
                    str(app / 'logging/sd_csv.c'), str(tmp / 'mock.c'), '-o', str(library)], check=True)
    lib = ct.CDLL(str(library))
    lib.outdoor_clock_at.argtypes = [ct.c_int64, ct.POINTER(ct.c_int64)]
    lib.outdoor_clock_at.restype = ct.c_bool
    lib.outdoor_clock_set_packet.argtypes = [ct.c_void_p, ct.c_size_t]
    lib.outdoor_clock_iso.argtypes = [ct.c_int64, ct.c_void_p, ct.c_size_t]
    lib.sd_csv_header.restype = ct.c_char_p
    lib.sd_csv_row.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_int64, ct.c_void_p, ct.c_size_t]
    epoch = ct.c_int64(-1)
    assert not lib.outdoor_clock_at(0, ct.byref(epoch)) and epoch.value == -1
    assert lib.outdoor_clock_set_packet(None, 12) == -errno.EINVAL
    assert lib.outdoor_clock_set_packet(b'wrong', 5) == -errno.EINVAL
    make = lambda epoch: b'OT\x01\0' + epoch.to_bytes(8, 'little')
    for invalid in (0, 1577836799999, 4102444800000, 2**64-1):
        assert lib.outdoor_clock_set_packet(make(invalid), 12) == -errno.ERANGE
    uptime = ct.c_int64.in_dll(lib, 'mock_uptime')
    uptime.value = 2500
    assert lib.outdoor_clock_set_packet(make(1791114000123), 12) == 0
    assert lib.outdoor_clock_at(2500 + 2 * 86400000, ct.byref(epoch))
    assert epoch.value == 1791114000123 + 2 * 86400000
    output = ct.create_string_buffer(4096)
    assert lib.outdoor_clock_iso(1835395200123, output, 4096) == 24
    assert output.value == b'2028-02-29T00:00:00.123Z'
    assert lib.outdoor_clock_iso(1791114000123, output, 24) == -errno.EINVAL
    vectors = json.loads((app.parent / 'software/tests/fixtures/wire.json').read_text())
    rows = {}
    header = lib.sd_csv_header().decode()
    for key in ('all', 'failed', 'current', 'empty'):
        packet = bytes.fromhex(vectors[key])
        n = lib.sd_csv_row(packet, len(packet), 1791114000123, output, len(output))
        assert n > 0 and output.raw[n-2:n] == b'\r\n'
        row = output.value.decode()
        records = list(csv.reader(io.StringIO(header + row)))
        assert len(records[0]) == len(records[1])
        rows[key] = dict(zip(*records))
        assert lib.sd_csv_row(packet, len(packet), 1791114000123, output, 10) == -errno.ENOSPC
    assert rows['all']['bme690.temperature_mc[°C]'] == '-1.234'
    assert rows['all']['bme690.pressure_pa[hPa]'] == '1018.03'
    assert rows['all']['sht45.temperature_mc[°C]'] == '23.274'
    assert rows['all']['sfa40.hcho_millippb[ppb]'] == '12.300'
    assert rows['all']['stcc4.serial_number'] == '0xfedcba9876543210'
    assert rows['all']['timestamp_utc'] == '2026-10-04T11:40:00.123Z'
    assert rows['failed']['bme690.error'] == '-5' and rows['failed']['bme690.pressure_pa[hPa]'] == ''
    assert rows['current']['sfa40.error'] == '' and rows['current']['sfa40.hcho_millippb[ppb]'] == ''
    packet_max = bytearray.fromhex(vectors['all'])
    offset = 24
    while packet_max[offset] != 5:
        offset += 4 + int.from_bytes(packet_max[offset+2:offset+4], 'little')
    packet_max[offset+4+26:offset+4+34] = (2**64-1).to_bytes(8, 'little')
    assert lib.sd_csv_row(bytes(packet_max), len(packet_max), -1, output, len(output)) > 0
    max_row = dict(zip(next(csv.reader(io.StringIO(header))), next(csv.reader(io.StringIO(output.value.decode())))))
    assert max_row['bmv080.runtime_ms[ms]'] == str(2**64-1)
    packet = bytes.fromhex(vectors['current'])
    n = lib.sd_csv_row(packet, len(packet), -1, output, len(output))
    assert n > 0 and output.value.startswith(b',0,')
    bad = bytearray(packet); bad[4] ^= 1
    assert lib.sd_csv_row(bytes(bad), len(bad), -1, output, len(output)) == -errno.EINVAL
    print('SD CSV: UTC/leap date, 48h clock continuity, packet validation, all sensors, signed/scaled fields, exact serials, missing/error/unsynced values, bounds OK')
