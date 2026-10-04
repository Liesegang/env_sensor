"""Read-only validation of an actual SD CSV exported by the outdoor board."""
import argparse
import csv
from datetime import datetime, timedelta
import hashlib
import io
import json
from pathlib import Path
from zoneinfo import ZoneInfo

parser = argparse.ArgumentParser()
parser.add_argument('csv_file', type=Path)
args = parser.parse_args()
schema = json.loads((Path(__file__).resolve().parents[2] / 'protocol/outdoor-v1.json').read_text())
data = args.csv_file.read_bytes()
records = list(csv.reader(io.StringIO(data.decode('utf-8-sig'))))
expected = ['timestamp_utc', 'clock_synced', 'sequence', 'uptime_ms', 'enabled_mask', 'valid_mask']
for sensor in schema['sensors']:
    expected.append(f"{sensor['key']}.error")
    for field in sensor['fields']:
        expected.append(f"{field.get('source', sensor['key'])}.{field['key']}" + (f"[{field['unit']}]" if field.get('unit') else ''))
assert records and records[0] == expected, 'Wrong or outdated CSV header'
assert data.endswith(b'\r\n'), 'Partial trailing record'
assert all(len(row) == len(expected) for row in records), 'Broken CSV row'
rows = [dict(zip(expected, row)) for row in records[1:]]
assert rows, 'No measurements'
gaps = []
errors = {}
synced = 0
clock_offsets = set()
previous = None
for row in rows:
    enabled, valid = int(row['enabled_mask']), int(row['valid_mask'])
    assert not enabled & ~0x7f and not valid & ~enabled
    sequence, uptime = int(row['sequence']), int(row['uptime_ms'])
    assert row['clock_synced'] in ('0', '1')
    if row['clock_synced'] == '1':
        epoch = datetime.fromisoformat(row['timestamp_utc'].replace('Z', '+00:00'))
        assert epoch.utcoffset() == timedelta(0)
        clock_offsets.add(round(epoch.timestamp() * 1000) - uptime)
        synced += 1
    else:
        assert row['timestamp_utc'] == ''
    if previous:
        delta = uptime - int(previous['uptime_ms'])
        seq_delta = sequence - int(previous['sequence'])
        assert delta > 0 and seq_delta > 0, 'Repeated or reordered measurements'
        if seq_delta != 1 or delta != 1000:
            gaps.append({'after_sequence': int(previous['sequence']), 'next_sequence': sequence,
                         'missing_samples': seq_delta - 1, 'delta_ms': delta})
    previous = row
    for sensor in schema['sensors']:
        error = row[f"{sensor['key']}.error"]
        on = bool(enabled & (1 << (sensor['id'] - 1)))
        if on:
            assert error != '' and int(error) <= 0
            assert bool(valid & (1 << (sensor['id'] - 1))) == (int(error) == 0)
            if int(error):
                key = f"{sensor['key']}:{error}"
                errors[key] = errors.get(key, 0) + 1
        else:
            assert error == ''
        for field in sensor['fields']:
            key = f"{field.get('source', sensor['key'])}.{field['key']}" + (f"[{field['unit']}]" if field.get('unit') else '')
            value = row[key]
            if not on or int(error):
                assert value == '', f'{key}: invalid measurement must stay blank'
            else:
                assert value != '', f'{key}: measurement is missing'
                if field['type'] == 'bool':
                    assert value in ('0', '1')
                elif field.get('format') == 'hex':
                    assert value.startswith('0x')
                    int(value, 16)
                else:
                    float(value)
local = lambda row: datetime.fromisoformat(row['timestamp_utc'].replace('Z', '+00:00')).astimezone(ZoneInfo('Asia/Tokyo')).isoformat() if row['timestamp_utc'] else None
print(json.dumps({'file': str(args.csv_file), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                  'rows': len(rows), 'columns': len(expected), 'synced_rows': synced,
                  'clock_offset_count': len(clock_offsets), 'first_jst': local(rows[0]), 'last_jst': local(rows[-1]),
                  'gaps': gaps, 'sensor_errors': errors, 'status': 'passed'}, ensure_ascii=False, indent=2))
