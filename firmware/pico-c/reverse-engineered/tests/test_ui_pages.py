#!/usr/bin/env python3
"""Compare recovered C page composition, pixels and cursor/font state with OEM ARM."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_oem import read_variant, VARIANTS
from test_control import OriginalC
from test_ui_graphics import ENTRIES

NAMES = ['pico_screen_brew_name_page', 'pico_screen_current_step_page',
         'pico_screen_recipe_metrics_page', 'pico_screen_step_progress_page',
         'pico_screen_temperature_page', 'pico_screen_program_status_page',
         'pico_screen_basic_brew_status', 'pico_screen_detailed_brew_status',
         'pico_ui_draw_hold_status', 'pico_screen_still_readings',
         'pico_screen_manual_overview_page', 'pico_screen_current_target_page',
         'pico_recipe_logo_page', 'pico_recipe_image_page', 'pico_ui_draw_circulation_status']
SOURCES = ['src/ui/' + name + '.c' for name in
           ('graphics', 'assets', 'context', 'encoder', 'display_driver', 'scrolling',
            'menu_render', 'display_helpers', 'display_values', 'pages')]
SOURCES += ['src/network/text.c', 'tests/ui_fixture.c', 'tests/pages_fixture.c']


def compile_native(variant, directory):
    output = directory / (variant + '.so')
    command = shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
        '-fno-fast-math', '-ffp-contract=off', '-fexcess-precision=standard',
        '-I' + str(ROOT / 'include'), '-DPICO_C_DISPLAY_SSD1309=' + str(int(variant == 'ssd1309')),
        '-Wl,--wrap=pico_display_temperature_summary', '-Wl,--wrap=pico_brew_display_state_label']
    subprocess.run(command + [str(ROOT / source) for source in SOURCES] + ['-lm', '-o', str(output)],
                   check=True)
    lib = ctypes.CDLL(str(output))
    lib.pico_test_ui_reset.argtypes = [ctypes.c_int] * 8 + [ctypes.c_uint]
    lib.pico_test_ui_framebuffer.restype = ctypes.POINTER(ctypes.c_ubyte)
    lib.pico_test_ui_state.argtypes = [ctypes.c_uint]
    lib.pico_test_ui_event.argtypes = [ctypes.c_uint, ctypes.c_uint]
    lib.pico_test_ui_event.restype = ctypes.c_uint32
    lib.pico_test_pages_configure.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_double)]
    lib.pico_test_pages_event.argtypes = [ctypes.c_uint, ctypes.c_uint]
    lib.pico_test_pages_event.restype = ctypes.c_uint32
    return lib


def verify(variant, directory):
    native = compile_native(variant, directory)
    original = OriginalC(variant)
    raw = read_variant(variant)
    delta = -24 if variant == 'ssd1309' else 0
    entries = dict(ENTRIES[variant])
    def literal(address):
        return struct.unpack_from('<I', raw, address - 0x20004000)[0]
    obj = literal(0x2000456c + (4 if variant == 'ssd1309' else 0))
    names = {entry['name']: int(entry[variant], 16) for entry in json.loads(
        (ROOT / 'analysis/manual-entry-points.json').read_text())['functions']}
    names['pico_ui_draw_circulation_status'] = 0x2000b110 + delta
    summary_events, hardware_events = [], []
    def summary(arm):
        words = [arm.reg(i) for i in range(4)] + [arm.read_u32(arm.reg('sp') + i * 4) for i in range(3)]
        summary_events.append([1, *words])
        return 0
    def state_label(arm):
        summary_events.append([2, arm.reg(0), arm.reg(1), 0, 0, 0, 0, 0])
        return original.SCRATCH
    original.hook(0x2000a558 + delta, summary)
    original.hook(0x200124f4 + delta, state_label)
    for name, kind, args in (('delay', 1, (0,)), ('pin_mode', 2, (0, 1)),
                             ('pin_write', 3, (0, 1)), ('spi_begin', 4, ()),
                             ('spi_transfer', 5, (1,))):
        def callback(arm, kind=kind, args=args):
            values = [arm.reg(index) for index in args]
            hardware_events.append([kind] + values + [0] * (2 - len(values)))
            return 0
        original.hook(entries[name], callback)
    original.hook(0x20004bc4 + (4 if variant == 'ssd1309' else 0), lambda arm: 0)
    cases = []
    baseline = [0, 0, 0, 4, 0, 0, 0, 3661, 0, 2]
    values = [150.25, 231.75, 155.9, 5.25, 73.75, 98.25, 120.5, 1.25]
    for operation in range(15):
        for language in (0, 1, 2):
            for celsius in (0, 1):
                for detailed in (0, 1):
                    config = list(baseline)
                    config[:3] = detailed, language, celsius
                    cases.append((operation, config, values))
    for component in range(1, 9):
        config = list(baseline); config[6] = component
        cases.append((3, config, values))
    for operation in (1, 5, 7, 8, 14):
        for seconds in (0, 59, 60, 3599, 3600, 86399):
            config = list(baseline); config[7] = seconds; config[5] = 1
            cases.append((operation, config, values))
    for phase in (0, 2, 4):
        for step in (0, 1, 2, 7, -1):
            config = list(baseline); config[3:5] = phase, step
            cases.append((6, config, values))
    for celsius in (0, 1):
        for still_celsius in (0, 1):
            config = list(baseline); config[2] = celsius; config[8] = still_celsius
            cases.append((9, config, values))
    records = []
    for index, (operation, config, input_values) in enumerate(cases):
        rotation = 0 if variant == 'ssd1309' else 2
        native.pico_test_ui_reset(1, rotation, 1, 1, 0, 0, 1, 1, 17)
        native.pico_test_pages_configure((ctypes.c_int32 * 10)(*config),
                                         (ctypes.c_double * 8)(*input_values))
        original.call(0x20004538)
        original.write(obj + 12, struct.pack('<hhhhHHBBBBI', 128,
                       32 if variant == 'ssd1309' else 64, 0, 0, 1, 1, 1, rotation, 1, 0, entries['font']))
        original.write(entries['buffer'], bytes((i * 37 + 17) & 255 for i in range(1024)))
        original.write(0x2002ba40, bytes(0x228))
        original.write(0x2002bc68, bytes(76))
        original.write(0x2002a480, bytes(0x238))
        original.write(0x2002a8b0, bytes(0x58))
        original.write(0x2002bcc3, bytes([config[0]]))
        original.write(0x2002a90c, bytes([config[1]]))
        original.write(literal(0x20008128 + delta), bytes([config[2]]))
        original.write(0x2002ba40 + 0x62, bytes([config[3]]))
        original.write_u32(0x2002a480 + 0x22c, config[4] & 0xffffffff)
        original.write(0x2002bc68 + 0x48, bytes([config[5]]))
        original.write_u32(0x2002bc68 + 0x40, config[6])
        original.write_u32(0x2002ba40 + 0x1a8, config[7] & 0xffffffff)
        original.write(0x2002ba40 + 8, bytes([config[8]]))
        original.write_u32(0x2002a8b0 + 0x30, config[9])
        original.write_u32(0x2002a480, 0x2002a8b0)
        original.write_u32(0x2002a8b0 + 0x40, original.SCRATCH + 32)
        for offset, location in ((0x48, 0x20004e60), (0x4c, 0x20004e64)):
            original.write_u32(0x2002a8b0 + offset, literal(location + (4 if variant == 'ssd1309' else 0)))
        for address, value in zip((0x2002bc30, 0x2002bc38, 0x2002bc10, 0x2002a698,
                                   0x2002ba50, 0x2002ba58, 0x2002ba60, 0x2002ba70), input_values):
            original.write(address, struct.pack('<d', value))
        original.write_u32(0x2002a694, 37)
        original.write_u32(0x2002bc68 + 0x44, 2)
        for address, value in ((0x2002bc68, b'Test Brew'), (0x2002bc88, b'Mash'),
                               (0x2002baa3, b'Heating'), (original.SCRATCH, b'STATE'),
                               (original.SCRATCH + 32, b'NOW BREWING')):
            original.write(address, value + b'\0')
        original.write(literal(0x200047f4 + (4 if variant == 'ssd1309' else 0)),
                       bytes((i * 13 + 7) & 255 for i in range(1024)))
        summary_events.clear(); hardware_events.clear()
        arguments = [*struct.unpack('<II', struct.pack('<d', input_values[0])), config[7]] if operation == 14 else []
        result = original.call(names[NAMES[operation]], arguments, max_instructions=1000000)
        native.pico_test_pages_run(operation)
        expected_pixels = original.read(entries['buffer'], 1024)
        actual_pixels = ctypes.string_at(native.pico_test_ui_framebuffer(), 1024)
        native_events = [[native.pico_test_pages_event(i, j) for j in range(8)]
                         for i in range(native.pico_test_pages_events())]
        actual_hardware = [[native.pico_test_ui_event(i, j) for j in range(3)]
                           for i in range(native.pico_test_ui_events())]
        width, height, x, y, foreground, background = struct.unpack('<hhhhHH', original.read(obj + 12, 12))
        expected_state = [x, y, width, height, int(original.read_u32(obj + 28) != 0),
                          original.read(obj + 25, 1)[0], foreground, background]
        actual_state = [native.pico_test_ui_state(i) for i in range(8)]
        if (expected_pixels != actual_pixels or summary_events != native_events
                or hardware_events != actual_hardware or expected_state != actual_state):
            mismatch = {'variant': variant, 'operation': operation, 'name': NAMES[operation],
                        'config': config, 'values': input_values,
                        'different_pixels': [i for i, (a, b) in enumerate(zip(expected_pixels, actual_pixels)) if a != b],
                        'expected_services': summary_events, 'actual_services': native_events,
                        'expected_display_state': expected_state, 'actual_display_state': actual_state,
                        'hardware_equal': hardware_events == actual_hardware}
            path = ROOT / 'build/ui-pages-failure.json'; path.write_text(json.dumps(mismatch, indent=2) + '\n')
            raise AssertionError((index, mismatch))
        records.append({'page': NAMES[operation], 'config': config,
                        'framebuffer_sha256': hashlib.sha256(expected_pixels).hexdigest(),
                        'original_instructions': result['instructions']})
    return {'cases': len(records), 'raw_sha256': VARIANTS[variant]['raw_sha256'], 'records': records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/ui-pages-tests.json')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-c-pages-') as temp:
        variants = {variant: verify(variant, Path(temp)) for variant in VARIANTS}
    report = {'schema_version': 1, 'variants': variants,
              'source_sha256': {source: hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
                                for source in SOURCES},
              'scope': 'Actual OEM ARM versus native C page composition and complete framebuffer, with only temperature-summary and state-label service boundaries substituted; real recovered fonts/assets/graphics.',
              'hardware_executed': False}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(sum(v['cases'] for v in variants.values()), 'original/native C page cases passed')


if __name__ == '__main__':
    main()
