#!/usr/bin/env python3
"""Recover the immutable data used by the original Basic console self-tests."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from analyze_images import parse_image

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = 'd84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
SAMPLES = [
    ('stats_samples', 0x3f41ed88, 18),
    ('stable_pressure', 0x3f424108, 643),
    ('unstable_pressure', 0x3f423048, 536),
    ('stable_t1', 0x3f421958, 734),
    ('unstable_t1', 0x3f41ff60, 831),
]


def outputs():
    raw = (ROOT.parent / 'oem/upstream/zseries_0_0_119.bin').read_bytes()
    if hashlib.sha256(raw).hexdigest() != IMAGE_SHA:
        raise ValueError('Unexpected original Z119 image')
    segments = parse_image(raw)['segments']

    def read(address, count):
        for part in segments:
            start = part['load_address']
            if part['memory_kind'] == 'DROM' and start <= address and address + count <= start + part['size_bytes']:
                at = part['file_offset'] + address - start
                return raw[at:at + count]
        raise ValueError('Diagnostic data is outside original DROM')

    def string(address):
        value = bytearray()
        for index in range(512):
            byte = read(address + index, 1)[0]
            if not byte:
                return bytes(value).decode('utf8')
            value.append(byte)
        raise ValueError('Unbounded diagnostic string')

    files, evidence = {}, []
    lines = ['// Original Stats.h self-test sample arrays; preserved Z119 flash addresses.']
    for name, address, count in SAMPLES:
        data = read(address, count * 8)
        values = struct.unpack('<' + 'd' * count, data)
        lines.append('static const double %s[] = { // 0x%x' % (name, address))
        for first in range(0, count, 8):
            lines.append('    ' + ','.join(format(value, '.17g') for value in values[first:first + 8]) + ',')
        lines.append('};')
        evidence.append(dict(name=name, address=f'0x{address:08x}', bytes=len(data),
                             sha256=hashlib.sha256(data).hexdigest()))
    files['src/basic_console_samples.inc'] = ('\n'.join(lines) + '\n').encode()

    table = read(0x3f426948, 22 * 4)
    lines = ['// Original UiManagement.h ParagraphText diagnostic strings at 0x3f426948.',
             'static const char* const paragraph_samples[] = {']
    for address in struct.unpack('<22I', table):
        value = string(address)
        lines.append('    ' + json.dumps(value, ensure_ascii=False) + ',')
        data = value.encode() + b'\0'
        evidence.append(dict(name='paragraph', address=f'0x{address:08x}', bytes=len(data),
                             sha256=hashlib.sha256(data).hexdigest()))
    lines.append('};')
    files['src/basic_console_paragraphs.inc'] = ('\n'.join(lines) + '\n').encode()

    address = 0x3f442569
    kind, reserved, instructions, strings = struct.unpack('<4I', read(address, 16))
    if (kind, reserved, instructions, strings) != (2, 0, 27, 7):
        raise ValueError('Unexpected original ProgramWriter fixture header')
    length = 16 + instructions * 8
    for _ in range(strings):
        size = read(address + length, 1)[0]
        if not size or read(address + length + size, 1) != b'\0':
            raise ValueError('Malformed original ProgramWriter string')
        length += size + 1
    data = read(address, length)
    lines = ['// Original ProgramWriter binary self-test payload at 0x3f442569.',
             'static const std::uint8_t writer_binary[] = {']
    for first in range(0, len(data), 16):
        lines.append('    ' + ','.join(f'0x{byte:02x}' for byte in data[first:first + 16]) + ',')
    lines.append('};')
    files['src/basic_console_binary.inc'] = ('\n'.join(lines) + '\n').encode()
    evidence.append(dict(name='writer_binary', address=f'0x{address:08x}', bytes=len(data),
                         sha256=hashlib.sha256(data).hexdigest()))
    report = dict(schema_version=1, input_sha256=IMAGE_SHA, original_data=evidence,
                  generated_source_sha256={path: hashlib.sha256(data).hexdigest() for path, data in files.items()},
                  scope='Exact immutable diagnostic inputs; the portable callbacks execute their operations in source. The OEM image is used only by offline extraction and validation.')
    files['analysis/basic-console-data.json'] = (json.dumps(report, indent=2) + '\n').encode()
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for path, data in outputs().items():
        destination = ROOT / path
        if args.check:
            if not destination.is_file() or destination.read_bytes() != data:
                raise SystemExit('Stale Basic diagnostic data: ' + path)
        else:
            destination.write_bytes(data)
    print('Verified original Basic diagnostic data' if args.check else 'Extracted original Basic diagnostic data')


if __name__ == '__main__':
    main()
