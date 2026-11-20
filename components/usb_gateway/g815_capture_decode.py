#!/usr/bin/env python3
import csv
import pathlib
import struct
import sys

HEADER = struct.Struct('<IHHQIHBBBBiHH')
MAGIC = 0x47383135
EVENTS = {
    1: 'attach', 2: 'detach', 3: 'input', 4: 'output',
    5: 'feature_get', 6: 'feature_set', 7: 'control_setup',
    8: 'control_in', 9: 'control_out', 10: 'error'
}

def main(path):
    source = pathlib.Path(path)
    target = source.with_suffix('.csv')
    with source.open('rb') as f, target.open('w', newline='', encoding='utf-8') as out:
        writer = csv.writer(out)
        writer.writerow(['sequence','timestamp_us','event','interface','report_id',
                         'endpoint','result','payload_length','original_length','hex'])
        while True:
            raw = f.read(HEADER.size)
            if not raw:
                break
            if len(raw) != HEADER.size:
                raise RuntimeError('truncated header')
            magic, version, header_size, timestamp, sequence, event_type, interface, report_id, endpoint, reserved, result, payload_length, original_length = HEADER.unpack(raw)
            if magic != MAGIC or header_size != HEADER.size:
                raise RuntimeError(f'invalid record at {sequence}')
            payload = f.read(payload_length)
            if len(payload) != payload_length:
                raise RuntimeError('truncated payload')
            writer.writerow([sequence, timestamp, EVENTS.get(event_type, event_type),
                             interface, report_id, f'0x{endpoint:02X}', result,
                             payload_length, original_length, payload.hex(' ')])
    print(target)

if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: g815_capture_decode.py events.bin')
    main(sys.argv[1])
