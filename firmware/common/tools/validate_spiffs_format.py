#!/usr/bin/env python3
"""Verify SPIFFS format ABI, deterministic output and complete native remounts."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
import build_spiffs_image as images
import spiffs_format as spiffs

PROBE = r'''
#include "spiffs/spiffs.h"
#include "spiffs/spiffs_nucleus.h"
#include <stddef.h>
const u32_t picobrew_spiffs_layout[] = {
    sizeof(spiffs_obj_id), sizeof(spiffs_block_ix), sizeof(spiffs_page_ix), sizeof(spiffs_span_ix),
    sizeof(spiffs_page_header), offsetof(spiffs_page_header,obj_id),
    offsetof(spiffs_page_header,span_ix), offsetof(spiffs_page_header,flags),
    sizeof(spiffs_page_object_ix_header), offsetof(spiffs_page_object_ix_header,p_hdr),
    offsetof(spiffs_page_object_ix_header,_align), offsetof(spiffs_page_object_ix_header,size),
    offsetof(spiffs_page_object_ix_header,type), offsetof(spiffs_page_object_ix_header,name),
    SPIFFS_OBJ_NAME_LEN, SPIFFS_OBJ_META_LEN,
#if SPIFFS_OBJ_META_LEN
    offsetof(spiffs_page_object_ix_header,meta),
#else
    0xffffffff,
#endif
    sizeof(spiffs_page_object_ix), offsetof(spiffs_page_object_ix,p_hdr),
    offsetof(spiffs_page_object_ix,_align), SPIFFS_USE_MAGIC_LENGTH, SPIFFS_GC_MAX_RUNS
};
'''


def probe_values(layout):
    page, obj, index = layout['page_header'], layout['object_header'], layout['index_header']
    return [layout[name] for name in ('object_id_bytes', 'block_index_bytes', 'page_index_bytes', 'span_index_bytes')] + \
        [page[name] for name in ('size', 'obj_id', 'span_ix', 'flags')] + \
        [obj[name] for name in ('size', 'p_hdr', '_align', 'file_size', 'type', 'name', 'name_bytes', 'metadata_bytes')] + \
        [obj.get('meta', 0xffffffff)] + [index[name] for name in ('size', 'p_hdr', '_align')] + \
        [layout['magic_length'], layout['gc_max_runs']]


def target_probe(esp8266, compiler, family, directory, expected):
    source, output = directory / (family + '-layout.c'), directory / (family + '-layout.o')
    source.write_text(PROBE)
    flags = ['-std=c99', '-Os', '-fdata-sections', '-I' + str(esp8266 / 'cores/esp8266')]
    if family == 'esp32':
        flags += ['-DSPIFFS_GC_MAX_RUNS=10', '-DSPIFFS_OBJ_META_LEN=4',
                  '-DSPIFFS_USE_MAGIC_LENGTH=1', '-DSPIFFS_ALIGNED_OBJECT_INDEX_TABLES=0']
    subprocess.run([str(compiler)] + flags + ['-c', str(source), '-o', str(output)], check=True)
    values = spiffs.elf_sections(output.read_bytes())['.rodata.picobrew_spiffs_layout']
    actual = list(struct.unpack('<' + 'I' * (len(values) // 4), values))
    if actual != probe_values(expected):
        raise ValueError('Pinned target compiler disagrees with native on-flash ABI: ' + family)
    return dict(compiler_sha256=spiffs.digest(compiler),
                object_sha256=spiffs.digest(output), values=actual, matches=True)


def fixtures(directory):
    result = {}
    for name, size in [('zero.a', 0), ('one.a', 1), ('page251.a', 251), ('page252.a', 252),
                       ('page256.a', 256), ('block.a', 8192), ('indices.a', 70001),
                       ('abcdefghijklmnopqrstuvw', 777)]:
        result['/pb/' + name] = bytes((index * 43 + size) & 255 for index in range(size))
    result['/pb/api.key'] = b'0123456789abcdef' * 4  # Synthetic test value only.
    network = bytes([1]) + b'format-test'.ljust(33, b'\0') + b'fixture-only'.ljust(65, b'\0')
    header = bytearray(struct.pack('<6I', 0x31564250, 1, len(network), 1, 0, zlib.crc32(b'network')))
    struct.pack_into('<I', header, 16, zlib.crc32(header[:16] + header[20:] + network))
    result['/pb/network.a'] = bytes(header) + network
    arguments = []
    for index, (name, data) in enumerate(result.items()):
        path = directory / ('synthetic-input-' + str(index))
        path.write_bytes(data)
        arguments.append(name + '=' + str(path))
    return result, arguments


def validate(esp8266, esp32, esp8266_toolchain, esp32_toolchain):
    identities = spiffs.verify_dependencies(esp8266, esp32)
    pin = json.loads((spiffs.ROOT / 'dependencies/spiffs-format-abi.json').read_text())
    with tempfile.TemporaryDirectory(prefix='picobrew-spiffs-format-validation-') as temporary:
        directory = Path(temporary)
        witness = spiffs.sdk_abi(esp32, esp32_toolchain / 'bin/xtensa-esp32-elf-readelf', directory)
        if witness != pin['esp32_dwarf_witness']:
            raise ValueError('Pinned SDK DWARF format witness changed')
        probes = {family: target_probe(esp8266, toolchain / ('bin/' + prefix + '-gcc'), family,
                                       directory, pin['layouts'][family])
                  for family, toolchain, prefix in
                  [('esp8266', esp8266_toolchain, 'xtensa-lx106-elf'),
                   ('esp32', esp32_toolchain, 'xtensa-esp32-elf')]}
        expected, arguments = fixtures(directory)
        profiles = {}
        for profile in spiffs.PROFILES:
            empty_path, staged_path = directory / (profile + '-empty.bin'), directory / (profile + '-staged.bin')
            empty = images.build(profile, esp8266, esp32, empty_path)
            staged = images.build(profile, esp8266, esp32, staged_path, arguments)
            duplicate = images.build(profile, esp8266, esp32, directory / 'duplicate.bin', list(reversed(arguments)))
            if staged['image_sha256'] != duplicate['image_sha256']:
                raise ValueError('Staging order changed the deterministic image')
            if staged_path.stat().st_mode & 0o077:
                raise ValueError('Staged private image permissions are not restrictive')
            data = bytearray(staged_path.read_bytes())
            data[252] ^= 1
            try:
                spiffs.inspect_image(bytes(data), profile, expected)
            except ValueError:
                rejected = True
            else:
                rejected = False
            if not rejected:
                raise ValueError('Independent decoder accepted corrupted block magic')
            profiles[profile] = dict(empty=empty, staged=staged, deterministic=True,
                                     corrupted_magic_rejected=True, staged_permissions_private=True)
        invalid = ['/pb/../x', '/pb/.hidden', '/pb/trailing.', '/pb/a..b', '/pb/a/b',
                   '/pb/cfg.a.tmp', '/pb/cfg.a.bak', '/pb/' + 'x' * 24, '/outside/key',
                   '/pb/', '/pb/cfg\\a', '/pb/a-b', '/pb/é']
        if any(spiffs.valid_logical_name(value) for value in invalid):
            raise ValueError('Invalid staged name accepted')
        key_path = directory / 'invalid-key'
        key_path.write_bytes(b'A' * 64)
        try:
            spiffs.staged_files(['/pb/api.key=' + str(key_path)], 0x200000)
        except ValueError:
            invalid_key_rejected = True
        else:
            invalid_key_rejected = False
        if not invalid_key_rejected:
            raise ValueError('Malformed explicit API key accepted')
    files = ['tools/spiffs_format.py', 'tools/spiffs_image_native.cpp', 'tools/build_spiffs_image.py',
             'tools/validate_spiffs_format.py', 'dependencies/spiffs-format-abi.json',
             'dependencies/esp8266-adapters.json', 'dependencies/esp32-spiffs-sizing.json']
    return dict(schema_version=1, profiles=profiles, target_layout_probes=probes,
                esp32_dwarf_witness=witness, sdk_identity_sha256=identities,
                invalid_logical_names_rejected=len(invalid), invalid_key_rejected=True,
                source_sha256={name: spiffs.digest(spiffs.ROOT / name) for name in files},
                hardware_tested=False, passed=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--esp8266-sdk', type=Path, required=True)
    parser.add_argument('--esp32-sdk', type=Path, required=True)
    parser.add_argument('--esp8266-toolchain', type=Path, required=True)
    parser.add_argument('--esp32-toolchain', type=Path, required=True)
    parser.add_argument('--write-report', action='store_true')
    args = parser.parse_args()
    report = validate(args.esp8266_sdk, args.esp32_sdk, args.esp8266_toolchain, args.esp32_toolchain)
    if args.write_report:
        path = spiffs.ROOT / 'analysis/spiffs-image-validation.json'
        path.parent.mkdir(exist_ok=True)
        path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(passed=report['passed'], profiles={name: dict(
        empty_sha256=value['empty']['image_sha256'], staged_sha256=value['staged']['image_sha256'],
        staged_files=value['staged']['native']['files'], deterministic=value['deterministic'])
        for name, value in report['profiles'].items()}, target_layouts_matched=True)))


if __name__ == '__main__':
    main()
