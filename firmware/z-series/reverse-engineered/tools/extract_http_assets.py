#!/usr/bin/env python3
"""Extract the eight original local HTTP resources from the Z119 DROM table."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from analyze_images import parse_image

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = 'd84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
TABLE = 0x3f4426a4
NAMES = ['help.html', 'lib.js', 'z.css', 'z.html', 'z.js', 'zstate.css', 'zstate.html', 'zstate.js']


def literal(data):
    pieces = []
    for byte in data:
        if byte == 10: pieces.append(r'\n')
        elif byte == 13: pieces.append(r'\r')
        elif byte in (34, 92): pieces.append('\\' + chr(byte))
        elif 32 <= byte <= 126: pieces.append(chr(byte))
        else: pieces.append('\\%03o' % byte)
    return '"' + ''.join(pieces) + '"'


def outputs():
    raw = (ROOT.parent / 'oem/upstream/zseries_0_0_119.bin').read_bytes()
    if hashlib.sha256(raw).hexdigest() != IMAGE_SHA: raise ValueError('Unexpected OEM image')
    image = parse_image(raw)
    def read(address, length):
        for segment in image['segments']:
            start = segment['load_address']
            if segment['memory_kind'] == 'DROM' and start <= address and address + length <= start + segment['size_bytes']:
                offset = segment['file_offset'] + address - start
                return raw[offset:offset + length]
        raise ValueError('Resource is outside original DROM')
    def string(address):
        return read(address, 256).split(b'\0', 1)[0].decode('ascii')
    sources = ['// Generated from the original Z119 HTTP resource table by extract_http_assets.py.',
               '// String literals preserve every original byte, including BOMs and CRLF endings.',
               '#include "zseries/local_http.hpp"', 'namespace zseries { namespace oem {', 'namespace {']
    files, entries = {}, []
    for index, expected in enumerate(NAMES):
        descriptor = read(TABLE + index * 16, 16)
        name_address, type_address, length, data_address = struct.unpack('<4I', descriptor)
        name, mime = string(name_address), string(type_address)
        if name != expected or not 0 < length < 32768: raise ValueError('Unexpected HTTP resource descriptor')
        data = read(data_address, length)
        if b'\0' in data: raise ValueError('Unexpected NUL inside textual HTTP resource')
        files['assets/http/' + name] = data
        sources.append('const char asset_%u[] =' % index)
        sources.extend('    ' + literal(line) for line in data.splitlines(keepends=True))
        sources[-1] += ';'
        entries.append(dict(name=name, content_type=mime, descriptor_address=f'0x{TABLE + index * 16:08x}',
            descriptor_sha256=hashlib.sha256(descriptor).hexdigest(), data_address=f'0x{data_address:08x}',
            bytes=length, sha256=hashlib.sha256(data).hexdigest()))
    sources.append('const LocalHttpAsset assets[LocalHttpAssetCount] = {')
    for index, entry in enumerate(entries):
        sources.append('    {%s, %s, reinterpret_cast<const std::uint8_t*>(asset_%u), sizeof(asset_%u) - 1},' %
                       (json.dumps(entry['name']), json.dumps(entry['content_type']), index, index))
    sources.extend(['};', '} // namespace',
                    'const LocalHttpAsset* local_http_asset(std::size_t index) noexcept {',
                    '    return index < LocalHttpAssetCount ? &assets[index] : nullptr;', '}',
                    '}} // namespace zseries::oem', ''])
    source = '\n'.join(sources).encode()
    files['src/http_assets.cpp'] = source
    report = dict(schema_version=1, input_sha256=IMAGE_SHA, table_address=f'0x{TABLE:08x}',
        default_path_address='0x3f418562', default_path=string(0x3f418562), assets=entries,
        generated_source_sha256=hashlib.sha256(source).hexdigest(), hardware_tested=False,
        scope='Exact original static resources and route-table metadata; no network or physical device behavior is established.')
    files['analysis/http-assets.json'] = (json.dumps(report, indent=2) + '\n').encode()
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for name, data in outputs().items():
        path = ROOT / name
        if args.check:
            if not path.exists() or path.read_bytes() != data: raise SystemExit('Stale HTTP resource: ' + name)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print('Verified eight original Z119 HTTP resources' if args.check else 'Extracted eight original Z119 HTTP resources')


if __name__ == '__main__': main()
