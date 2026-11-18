#!/usr/bin/env python3
"""Build and inspect the separate CC3200 web recovery candidate; never install it.

The fixed 32 KiB heap and native 32 KiB stack remain intact. This build uses no
application/controller archives and fails if its raw image exceeds 56 KiB.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
ORIGIN, END = 0x20004000, 0x20040000
IMAGE_LIMIT = 57344


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect_image(elf_path, binary_path):
    raw = binary_path.read_bytes()
    with elf_path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf['e_machine'] != 'EM_ARM' or elf.elfclass != 32 or not elf.little_endian:
            raise ValueError('Recovery must be little-endian 32-bit ARM')
        entries = list(elf.get_section_by_name('.symtab').iter_symbols())
        symbols = {s.name: int(s['st_value']) for s in entries if s['st_shndx'] != 'SHN_UNDEF'}
        sizes = {s.name: int(s['st_size']) for s in entries if s['st_shndx'] != 'SHN_UNDEF'}
        undefined = [s.name for s in entries if s.name and s['st_shndx'] == 'SHN_UNDEF']
        if undefined:
            raise ValueError('Undefined recovery symbols: ' + repr(undefined))
        prohibited = ('pico_application_', 'pico_recipe_', 'pico_actuator_',
                      'pico_temperature_control_', 'pico_heater_pump_', 'fixture',
                      'sdk_validation_', 'pico_validation_entry')
        forbidden = [name for name in symbols if any(part in name for part in prohibited)]
        if forbidden:
            raise ValueError('Brewing/test implementation reached recovery: ' + repr(forbidden))
        for name in ('sl_FsDel', 'sl_FsFormat', 'sl_WlanProfileDel', 'sl_WlanProfileAdd'):
            if name in symbols:
                raise ValueError('Prohibited destructive service reached recovery: ' + name)
        required = ('ResetISR', 'main', 'setup', 'loop', 'g_pfnVectors',
                    '_heap', '_eheap', '_bss', '_ebss', 'pui32Stack')
        missing = [name for name in required if name not in symbols]
        if missing:
            raise ValueError('Missing native recovery boundary: ' + repr(missing))
        segments = []
        for segment in elf.iter_segments():
            if segment['p_type'] != 'PT_LOAD':
                continue
            start, memory, loaded = (int(segment[k]) for k in ('p_paddr', 'p_memsz', 'p_filesz'))
            if start < ORIGIN or start + memory > END or int(segment['p_vaddr']) != start:
                raise ValueError('Recovery load segment lies outside application SRAM')
            segments.append({'start': hex(start), 'memory_bytes': memory, 'file_bytes': loaded})
        if len(raw) < 8:
            raise ValueError('Recovery vector table missing')
        stack, reset = struct.unpack_from('<II', raw)
        if symbols['g_pfnVectors'] != ORIGIN:
            raise ValueError('Recovery vector table has the wrong address')
        if sizes['pui32Stack'] != 32768 or stack != symbols['pui32Stack'] + 32768 or stack & 7:
            raise ValueError('Recovery must preserve the native aligned 32 KiB stack')
        if not (symbols['_bss'] < stack <= symbols['_ebss']):
            raise ValueError('Recovery stack lies outside native BSS')
        if not reset & 1 or (reset & ~1) != (symbols['ResetISR'] & ~1):
            raise ValueError('Recovery reset vector is invalid')
        if (int(elf['e_entry']) & ~1) != (reset & ~1):
            raise ValueError('Recovery raw and ELF reset entries disagree')
        if symbols['_eheap'] - symbols['_heap'] != 32768 or symbols['_eheap'] > END:
            raise ValueError('Recovery must preserve a bounded 32 KiB heap')
        loaded_end = max(int(s['start'], 16) + s['file_bytes'] for s in segments if s['file_bytes'])
        if len(raw) != loaded_end - ORIGIN:
            raise ValueError('Recovery raw image differs from ELF loadable extent')
        # Endpoints/paths that could contact the old service must not reappear.
        if b'picobrew.com' in raw or b'/API/pico/' in raw:
            raise ValueError('Cloud protocol reached recovery')
        return {
            'passed': len(raw) <= IMAGE_LIMIT,
            'load_address': hex(ORIGIN), 'ram_end': hex(END),
            'initial_stack_pointer': hex(stack), 'reset_vector': hex(reset),
            'binary_bytes': len(raw), 'image_limit_bytes': IMAGE_LIMIT,
            'image_headroom_bytes': IMAGE_LIMIT - len(raw),
            'bss_bytes': symbols['_ebss'] - symbols['_bss'],
            'heap_bytes': symbols['_eheap'] - symbols['_heap'], 'sdk_stack_bytes': 32768,
            'unallocated_ram_bytes': END - symbols['_eheap'], 'load_segments': segments,
            'undefined_symbols': [], 'brewing_or_test_symbols': [],
            'required_entry_symbols': {name: hex(symbols[name]) for name in required}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm-prefix', default=os.environ.get('PICOBREW_ARM_PREFIX'))
    parser.add_argument('--sdk-root', type=Path,
                        default=Path(os.environ.get('PICOBREW_SDK', str(ROOT / '.deps/energia'))))
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/recovery')
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/embed_recovery_web.py'), '--check'],
                   check=True)
    sources = [ROOT / path for path in ('recovery/recovery.c', 'recovery/sha256.c',
               'recovery/http.c', 'recovery/web_assets.c', 'src/http_server.c')]
    missing = [str(path.relative_to(ROOT)) for path in sources if not path.is_file()]
    if missing:
        parser.error('Recovery inputs missing (generate embedded web assets first): ' + ', '.join(missing))
    command = [sys.executable, str(ROOT / 'tools/build_sdk_validation.py'),
               '--recovery', '--entry', str(ROOT / 'recovery/native.cpp'),
               '--sdk-root', str(args.sdk_root.resolve()), '--output-dir', str(out / 'native'),
               '--linker', str(ROOT / 'platform/pico_cc3200.ld'),
               '--include', str(ROOT / 'recovery')]
    if args.arm_prefix:
        command += ['--arm-prefix', args.arm_prefix]
    for source in sources:
        command += ['--source', str(source)]
    result = subprocess.run(command, text=True, capture_output=True)
    (out / 'build.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        return result.returncode
    name = 'pico-web-recovery'
    for suffix in ('.elf', '.bin', '.map'):
        shutil.copy2(out / 'native' / ('sdk_validation' + suffix), out / (name + suffix))
    elf, binary = out / (name + '.elf'), out / (name + '.bin')
    layout = inspect_image(elf, binary)
    native = json.loads((out / 'native/validation.json').read_text())
    compiled = json.loads((out / 'native/compile_report.json').read_text())
    report = {
        'artifact': 'Independent Pico web recovery candidate', 'profile': 'recovery',
        'firmware_version': re.search(r'^#define PICO_RECOVERY_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"$',
                                     (ROOT / 'recovery/recovery.h').read_text(), re.MULTILINE).group(1),
        'hardware_executed': False, 'installation_supported': False,
        'production_entry': 'recovery/native.cpp', 'image_layout': layout,
        'binary_sha256': digest(binary), 'elf_sha256': digest(elf),
        'sdk_link_passed': native['pass'], 'runtime_boundary': native['runtime_boundary'],
        'compiled_sources': [{'source': item['source'], 'sha256': item['source_sha256']}
                             for item in compiled],
        'project_sources_sha256': {
            str(path.relative_to(ROOT)): digest(path)
            for path in sorted([p for p in (ROOT / 'recovery').rglob('*') if p.is_file()] +
                               [ROOT / p for p in ('src/http_server.c', 'include/picobrew/http_server.h',
                                'platform/pico_cc3200.ld', 'platform/energia_runtime_compat.c',
                                'tools/build_sdk_validation.py', 'tools/build_recovery.py',
                                'tools/embed_recovery_web.py', 'dependencies/lock.json')])},
        'constraints': {'brewing_modules_linked': False, 'image1_write_available': False,
                        'application_slot_creation_available': False,
                        'raw_flash_delete_format_available': False,
                        'normal_selector_zero_required_for_mutations': True,
                        'embedded_recovery_assets': True,
                        'upload_inactivity_timeout_ms': 30000,
                        'upload_timeout_waits_for_http_idle': True},
        'validation_scope': 'Native compile/link, vector/layout, symbol exclusions and size checks; physical startup, radio, actuator pins, flash writes and power-failure recovery are not exercised.',
        'files': {'elf': elf.name, 'binary': binary.name, 'map': name + '.map'}}
    (out / 'recovery-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'image': str(binary), 'binary_bytes': layout['binary_bytes'],
                      'image_limit_bytes': IMAGE_LIMIT,
                      'unallocated_ram_bytes': layout['unallocated_ram_bytes'],
                      'image_layout_passed': layout['passed'], 'hardware_executed': False}, indent=2))
    if not layout['passed']:
        print('Recovery exceeds its conservative image1 capacity; candidate must not be installed.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
