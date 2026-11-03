#!/usr/bin/env python3
"""Build the actual C codec and check wire bytes independently with struct/zlib."""
import json
import argparse
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import zlib

here = Path(__file__).resolve().parent
codec = here.parent
root = here.parents[3]
schema = json.loads((root / "shared/protocol/env-history-v1.json").read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--write-fixtures", action="store_true")
args = parser.parse_args()
subprocess.run(["python3", str(root / "shared/protocol/generate_history.py"), "--check"], check=True)
with tempfile.TemporaryDirectory(prefix="env-history-codec-") as directory:
    binary = Path(directory) / "codec_check"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(codec / "include"),
                    str(here / "codec_check.c"), str(codec / "env_history_codec.c"),
                    "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
vectors = {line.split("=", 1)[0]: bytes.fromhex(line.split("=", 1)[1]) for line in result.stdout.splitlines()}
values = [1001, 300, 1780000000, -12345, 45678, -123, -23456, 67890, 100123,
          1234567, 0x12345678, 1234, 50000, 65535, 321, 123, 0xabcd, 0x55aa, 0x1fff, 1, 1, 0, 0]
record_format = "<IIQiIiiIIIIhHHHHHHHBBBB"
expected_record = struct.pack(record_format, *values)
assert len(expected_record) == schema["formats"]["record"]["size"] == 68
assert vectors["record"] == expected_record
canonical = values.copy()
for index in [4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15]:
    canonical[index] = 0
canonical[18] = 1 | 256
assert vectors["canonical"] == struct.pack(record_format, *canonical)
limits = values.copy()
limits[0] = limits[1] = limits[4] = limits[10] = 0xffffffff
limits[3] = -2147483648
limits[11] = -32768
assert vectors["signed_limits"] == struct.pack(record_format, *limits)
unknown = values.copy()
unknown[2] = 0
assert vectors["unknown_time"] == struct.pack(record_format, *unknown)
payload = b""
for at in range(6):
    row = values.copy()
    row[0] += at; row[1] += at; row[2] += at
    payload += struct.pack(record_format, *row)
page = bytearray(struct.pack("<2sBBIQIHHII", b"HP", 1, 1, 0x12345678, 0x9abcdef012345678, 1001, 6, 68, 0, 7) + payload)
crc = zlib.crc32(page)
struct.pack_into("<I", page, 24, crc)
assert vectors["page"] == bytes(page)
gap_page = page.copy()
gap_page[3] = 3
struct.pack_into("<I", gap_page, 24, 0)
struct.pack_into("<I", gap_page, 32 + 5 * 68, 1007)
struct.pack_into("<I", gap_page, 24, zlib.crc32(gap_page))
assert vectors["page_gap"] == bytes(gap_page)
expected_status = struct.pack("<2sBBIQIQIIHHIiQIQI", b"HS", 1, 2, 0x12345678,
    0x8000000000000001, 1, 0x1122334455667788, 123456, 3, 6, 440, 1006, 0,
    0x1122334455667788, 7, 1780000300, 600)
assert len(expected_status) == 72
assert vectors["status"] == expected_status
assert vectors["hint"] == struct.pack("<2sBBI", b"HN", 1, 2, 0x12345678)
error_status = bytearray(expected_status)
error_status[3] = 4
error_status[36:44] = bytes(8)
struct.pack_into("<i", error_status, 44, -5)
assert vectors["error_status"] == bytes(error_status)
assert vectors["pull_oldest"] == struct.pack("<2sBBIQI", b"HC", 1, 1, 0x12345678, 0, 0)
assert vectors["ack"] == struct.pack("<2sBBIQI", b"HC", 1, 2, 0x12345678, 0x11, 0x22)
assert vectors["cancel"] == struct.pack("<2sBBIQI", b"HC", 1, 3, 0x12345678, 0, 0)
assert vectors["pull_after"] == struct.pack("<2sBBIQI", b"HC", 1, 1, 0xa1b2c3d4, 0x8fedcba987654321, 0xffffffff)
assert vectors["clock_state"] == struct.pack("<2sBBQIQQ", b"CT", 1, 1, 0x1122334455667788, 7, 1780000300000, 600000)
assert vectors["clock_unknown"] == struct.pack("<2sBBQIQQ", b"CT", 1, 0, 0x1122334455667788, 0, 0, 0)
assert vectors["clock_write"] == struct.pack("<2sBBQ", b"OT", 1, 0, 1780000300123)
fixture = {
    "schema": "env-history-v1",
    "description": "Generated after independent Python struct.pack/zlib validation against the actual C codec; UInt64 identities are hexadecimal strings.",
    "page": {"requestId": 0x12345678, "bootId": "9abcdef012345678", "clockRevision": 7,
             "firstSequence": 1001, "lastSequence": 1006, "count": 6, "size": 440, "flags": 1,
             "crc32": f"{crc:08x}"},
    "record": {field["name"]: value for field, value in zip(schema["formats"]["record"]["fields"], values)},
    "signedLimitsRecord": {field["name"]: value for field, value in zip(schema["formats"]["record"]["fields"], limits)},
    "status": {"state": 2, "currentBootId": "1122334455667788", "clockRevision": 7,
               "utcAnchorS": 1780000300, "uptimeAnchorS": 600},
    "clockState": {"flags": 1, "bootId": "1122334455667788", "clockRevision": 7,
                   "unixAnchorMs": 1780000300000, "uptimeAnchorMs": 600000},
    "vectors": {key: value.hex() for key, value in sorted(vectors.items())},
}
fixture_path = root / "shared/protocol/tests/env-history-v1-golden.json"
if args.write_fixtures:
    fixture_path.parent.mkdir(parents=True, exist_ok=True)
    fixture_path.write_text(json.dumps(fixture, indent=2, sort_keys=True) + "\n")
else:
    assert json.loads(fixture_path.read_text()) == fixture, "History golden fixture is stale"
print("History codec host checks PASS: ASan/UBSan; explicit LE; signed limits; canonical RAM/CSV replay; "
      "440-byte 6-record page; independent IEEE CRC; bounds; gaps; HC20 malformed/truncated; "
      "HS72/HN8 and clock anchors; dedicated CT32/OT12 known/unknown clock")
