"""Execute the firmware's C decoders on documented values and damaged packets."""

import ctypes as ct
import errno
from pathlib import Path
import subprocess
import tempfile
import unittest

app_dir = Path(__file__).resolve().parents[1]
temporary = tempfile.TemporaryDirectory(prefix="outdoor-codec-")
library_path = Path(temporary.name) / "sensor_codec.dylib"
subprocess.run(
    [
        "/usr/bin/cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
        "-I", str(app_dir / "driver/common/include"),
        "-I", str(app_dir / "driver/sht45/include"),
        str(app_dir / "driver/sht45/sht45.c"),
        str(app_dir / "driver/common/sensor_codec.c"), "-o", str(library_path),
    ],
    check=True,
)
library = ct.CDLL(str(library_path))
library.sensirion_crc8.argtypes = [ct.POINTER(ct.c_uint8), ct.c_size_t]
library.sensirion_crc8.restype = ct.c_uint8
library.sensirion_decode_words.argtypes = [
    ct.POINTER(ct.c_uint8), ct.c_size_t, ct.POINTER(ct.c_uint16), ct.c_size_t,
]
library.opt4001_decode_result.argtypes = [ct.c_uint16, ct.c_uint16, ct.POINTER(ct.c_uint32)]
for name in ("sht45_temperature_mc", "sht45_humidity_mpercent"):
    function = getattr(library, name)
    function.argtypes = [ct.c_uint16]
    function.restype = ct.c_int32
for name in ("sgp41_temperature_ticks", "sgp41_humidity_ticks"):
    function = getattr(library, name)
    function.argtypes = [ct.c_int32]
    function.restype = ct.c_uint16


def byte_array(values):
    return (ct.c_uint8 * len(values))(*values)


class ProtocolTests(unittest.TestCase):
    def test_sensirion_crc_reference_vectors(self):
        for data, expected in (([0xBE, 0xEF], 0x92), ([0x80, 0x00], 0xA2),
                               ([0x66, 0x66], 0x93), ([0xFF, 0xFF], 0xAC)):
            self.assertEqual(library.sensirion_crc8(byte_array(data), 2), expected)

    def test_sensirion_decodes_all_words(self):
        packet = byte_array([0x80, 0x00, 0xA2, 0x66, 0x66, 0x93])
        words = (ct.c_uint16 * 2)()
        self.assertEqual(library.sensirion_decode_words(packet, 6, words, 2), 0)
        self.assertEqual(list(words), [0x8000, 0x6666])

    def test_sensirion_rejects_each_damaged_bit_without_partial_output(self):
        original = [0x80, 0x00, 0xA2, 0x66, 0x66, 0x93]
        for index in range(48):
            data = original.copy()
            data[index // 8] ^= 1 << (index % 8)
            words = (ct.c_uint16 * 2)(0x1111, 0x2222)
            self.assertEqual(library.sensirion_decode_words(byte_array(data), 6, words, 2),
                             -errno.EBADMSG)
            self.assertEqual(list(words), [0x1111, 0x2222])

    def test_sensirion_rejects_short_packet(self):
        words = (ct.c_uint16 * 2)(0x1111, 0x2222)
        self.assertEqual(library.sensirion_decode_words(byte_array([0] * 5), 5, words, 2),
                         -errno.EINVAL)
        self.assertEqual(list(words), [0x1111, 0x2222])

    def test_sht45_units_and_physical_humidity_bounds(self):
        self.assertEqual(library.sht45_temperature_mc(0), -45000)
        self.assertEqual(library.sht45_temperature_mc(65535), 130000)
        self.assertEqual(library.sht45_temperature_mc(32768), 42501)
        self.assertEqual(library.sht45_humidity_mpercent(0), 0)
        self.assertEqual(library.sht45_humidity_mpercent(65535), 100000)
        self.assertEqual(library.sht45_humidity_mpercent(32768), 56500)

    def test_sgp41_compensation_defaults_fractional_values_and_bounds(self):
        self.assertEqual(library.sgp41_temperature_ticks(25000), 0x6666)
        self.assertEqual(library.sgp41_humidity_ticks(50000), 0x8000)
        self.assertEqual(library.sgp41_temperature_ticks(25500), 26401)
        for value, expected in ((-46000, 0), (131000, 65535)):
            self.assertEqual(library.sgp41_temperature_ticks(value), expected)
        for value, expected in ((-1, 0), (100001, 65535)):
            self.assertEqual(library.sgp41_humidity_ticks(value), expected)

    def test_opt4001_dts_lux_scale_counter_and_exponent(self):
        # R=16, E=0/1/8; counter=0/7. Last case is full scale, E=8.
        fixtures = [(0x0000, 0x1001, 7), (0x1000, 0x1000, 14),
                    (0x8000, 0x1006, 1792), (0x0000, 0x1072, 7),
                    (0x8FFF, 0xFFFF, 117440400)]
        for msb, lsb, expected in fixtures:
            result = ct.c_uint32()
            self.assertEqual(library.opt4001_decode_result(msb, lsb, ct.byref(result)), 0)
            self.assertEqual(result.value, expected)

    def test_opt4001_rejects_each_damaged_bit(self):
        for index in range(32):
            packed = (0x80001006 ^ (1 << index))
            result = ct.c_uint32(1234)
            self.assertLess(library.opt4001_decode_result(packed >> 16, packed & 65535,
                                                         ct.byref(result)), 0)
            self.assertEqual(result.value, 1234)

    def test_opt4001_rejects_reserved_range_with_valid_crc(self):
        result = ct.c_uint32(1234)
        self.assertEqual(library.opt4001_decode_result(0x9000, 0x1007, ct.byref(result)),
                         -errno.ERANGE)
        self.assertEqual(result.value, 1234)


if __name__ == "__main__":
    try:
        unittest.main(verbosity=2)
    finally:
        temporary.cleanup()
