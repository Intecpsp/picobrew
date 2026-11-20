#!/usr/bin/env python3
"""Compare C workflow branches, text, callbacks, menu/state and operation order.

The original instructions run independently. Scrolling-message interaction,
menu input, alarm, and physical services are explicit scripted boundaries;
this suite does not operate a machine or prove physical equivalence.
"""
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
from test_control import OriginalC

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_oem import VARIANTS

ENTRIES = [
    ('deep_clean_setup', 0x20009dd0, [0], False),
    ('first_rinse_warning', 0x20009b0c, [0], False),
    ('vacuum', 0x2000dee4, list(range(5)), True),
    ('manual_network', 0x2000e3f0, list(range(4)), False),
    ('picopak_association', 0x20009b54, [0], False),
    ('rinse_finished', 0x20009e10, [0], False),
    ('first_rinse', 0x20009bc4, list(range(12)), False),
    ('rack_beer', 0x2000b7f4, list(range(10)), False),
    ('descale', 0x2000eaa4, list(range(9)), True),
    ('filter_wait', 0x20009eb0, [0], False),
    ('resume', 0x2000dcd4, [0], True),
    ('program_steps', 0x2000dd14, [0, 1, 2], True),
    ('thermal_recovery', 0x2000ebe8, [0, 5, 6, 7], True),
    ('screen_confirm', 0x2000eb80, [0], True),
    ('program_setup_choice', 0x2000dc90, [0], True),
    ('wait_done', 0x2000e6b0, [0], False),
    ('wait_continue', 0x2000e394, [0], True),
]
SOURCES = ['src/ui/' + n + '.c' for n in ('ui_workflows', 'workflow_adapters',
    'ui_program_menus', 'screens', 'ui_screens', 'instruction_screens',
    'translations', 'strings', 'assets')] + ['tests/workflow_fixture.c']


def compile_native(directory):
    output = directory / 'workflows.so'
    command = shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared',
        '-fvisibility=hidden', '-ffunction-sections', '-fdata-sections',
        '-Wl,--gc-sections,-z,defs', '-I' + str(ROOT / 'include')]
    command += ['-Wl,--wrap=' + name for name in ('pico_screen_still_setup_step',
                'pico_screen_water_error_help', 'pico_screen_thermal_recovery_help')]
    subprocess.run(command + [str(ROOT / s) for s in SOURCES] + ['-o', str(output)], check=True)
    native = ctypes.CDLL(str(output))
    native.pico_test_workflow_event.restype = ctypes.c_uint32
    native.pico_test_workflow_text.restype = ctypes.c_char_p
    native.pico_test_workflow_labels.restype = ctypes.c_void_p
    return native


def verify(variant, native):
    original = OriginalC(variant)
    delta = -24 if variant == 'ssd1309' else 0
    early = 4 if variant == 'ssd1309' else 0
    labels, scrolling, program = 0x2002a6cc, 0x2002c2c0, 0x2002a8b0
    events, config = [], []
    def event(kind, a=0, b=0, text=b''):
        events.append([kind, a & 0xffffffff, b & 0xffffffff, text.hex()])
        return 0
    def cstring(address):
        return original.cstring(address)
    callback_addresses = {address + delta: number for address, number in (
        (0x2000b2a4, 1), (0x2000b2b8, 2), (0x2000b2cc, 3), (0x2000b0a4, 4),
        (0x2000b0b8, 5), (0x2000b0e4, 6), (0x2000b218, 7), (0x2000b244, 8),
        (0x2000b270, 9))}
    def scroll(r, prompt=False):
        pointer, callback = (r.reg(0), r.reg(1)) if prompt else (r.reg(1), r.reg(2))
        cb = callback & ~1
        if cb and cb not in callback_addresses:
            raise AssertionError(('Unknown callback', hex(cb)))
        event(14 if prompt else 6, callback_addresses.get(cb, 0), 0, cstring(pointer))
        original.write(scrolling + 0x130, b'\0' if prompt else b'\1')
    comparison = json.loads((ROOT / 'analysis/function-correspondence.json').read_text())[
        'variants'][VARIANTS[variant]['display']]['comparison']
    named = {x['name']: int(x['target'], 16) for x in comparison['candidates'] + comparison['different_by_call_context']}
    original.hook(named['pico_assert_nonfatal'], lambda r: 0)
    original.hook(0x20004488, lambda r: event(1))
    original.hook(0x20004494, lambda r: event(2))
    original.hook(0x200054ac + (-20 if variant == 'ssd1309' else 0),
                  lambda r: event(3, r.reg(0), 0, cstring(r.reg(1))))
    original.hook(0x20005474 + (-20 if variant == 'ssd1309' else 0),
                  lambda r: event(4, r.reg(0), r.reg(1), cstring(r.reg(2))))
    original.hook(0x20009944 + delta, lambda r: event(5, r.reg(0)))
    original.hook(0x20004bd0 + early, scroll)
    original.hook(0x20009a84 + delta, lambda r: scroll(r, True))
    original.hook(0x2000452c, lambda r: event(7, r.reg(0), 0, cstring(r.reg(1))))
    original.hook(0x20018922 + (-164 if variant == 'ssd1309' else 0), lambda r: event(8))
    original.hook(0x20018918 + (-164 if variant == 'ssd1309' else 0), lambda r: event(9))
    original.hook(0x200050b8 + early, lambda r: (event(10, config[2]), config[2])[1])
    original.hook(0x20017d4c + (-164 if variant == 'ssd1309' else 0), lambda r: event(11, r.reg(0)))
    original.hook(0x2000b080 + delta, lambda r: event(12, 0, 0, cstring(r.reg(0))))
    original.hook(0x20004adc + early,
                  lambda r: event(13, 0, 0, b'|'.join(cstring(r.reg(i)) for i in range(3))))
    original.hook(0x2000db56 + delta,
                  lambda r: (event(15, r.reg(0), r.reg(1), str(r.reg(2)).encode()), config[3])[1])
    original.hook(0x2000b7a0 + delta, lambda r: event(16, original.read(0x2002c254, 1)[0]))
    original.hook(0x200045f2 + early, lambda r: event(17))
    original.hook(0x2000ea78 + delta, lambda r: (event(18, r.reg(0), int(bool(config[3]))), int(bool(config[3])))[1])
    original.hook(0x20009ea0 + delta, lambda r: event(20))
    original.hook(0x20006808 + delta, lambda r: event(21, original.read(r.reg(0), 1)[0]))
    original.hook(0x20008d2c + delta, lambda r: (event(22, r.reg(0)), 3)[1])
    original.hook(0x2000b970 + delta, lambda r: event(23, r.reg(0)))
    original.hook(0x2000be20 + delta, lambda r: event(24))
    for name, value in (('pico_steam_pump_on', 1), ('pico_steam_pump_off', 2),
                         ('pico_drain_pump_on', 3), ('pico_drain_pump_off', 4)):
        original.hook(named[name], lambda r, value=value: event(25, value))
    def recovery_help(value):
        event(26, value)
        original.write(scrolling + 0x130, b'\1')
    original.hook(0x20004c64 + early, lambda r: recovery_help(0))
    original.hook(0x20004ca8 + early, lambda r: recovery_help(1))
    traces = []
    for operation, (name, entry, values, returns) in enumerate(ENTRIES):
        for language in (0, 1, 2):
            for choice in (0, 1, 3):
                sessions = (0, 1, 2, 3, 4) if operation == 14 else (0,)
                filters = (0, 1) if operation == 9 else (1,)
                for session in sessions:
                    for filter_value in filters:
                        for value in values:
                            config[:] = [language, session, filter_value, choice]
                            native.pico_test_workflow_reset(*config)
                            original.write(0x2002a90c, bytes([language]))
                            original.write(labels, b'Z' * 484)
                            original.write(0x2002c254, b'\x11')
                            original.write(0x2002a6b8, bytes(20))
                            original.write(scrolling + 0x130, b'\1')
                            original.write_u32(0x2002a480, program)
                            original.write_u32(program + 0x30, session)
                            original.write_u32(program + 0x24, 149)
                            original.write_u32(program + 0x28, 3661)
                            for offset, address, text in ((0x34, original.SCRATCH, b'TITLE'),
                                (0x38, original.SCRATCH + 256, b'Recipe'),
                                (0x3c, original.SCRATCH + 512, b'Brewing'),
                                (0x44, original.SCRATCH + 768, b'Paused session')):
                                original.write(address, text + b'\0')
                                original.write_u32(program + offset, address)
                            original.write(0x2002bc68, b'Recipe\0')
                            events.clear()
                            args = [value]
                            if operation == 13:
                                args = [0x2000b0a5 + delta]
                            result = original.call(entry + delta, args, max_instructions=200000)
                            actual_return = native.pico_test_workflow_run(operation, value)
                            actual = [[native.pico_test_workflow_event(i, j) for j in range(3)] +
                                [native.pico_test_workflow_text(i).hex()] for i in range(native.pico_test_workflow_events())]
                            expected_state = [original.read(scrolling + 0x130, 1)[0],
                                *original.read(0x2002a6b8, 4), original.read(0x2002a6c8, 1)[0],
                                original.read(0x2002c254, 1)[0]]
                            actual_state = [native.pico_test_workflow_state(i) for i in range(7)]
                            if (events != actual or expected_state != actual_state or
                                    (returns and result['r0'] != actual_return)):
                                raise AssertionError((variant, name, config, value, events, actual,
                                    expected_state, actual_state, result['r0'], actual_return))
                            expected_labels = original.read(labels, 484)
                            actual_labels = ctypes.string_at(native.pico_test_workflow_labels(), 484)
                            if expected_labels != actual_labels:
                                raise AssertionError((variant, name, config, 'menu labels', expected_labels, actual_labels))
                            traces.append([name, config[:], value, events[:], expected_state,
                                           result['r0'] if returns else None])
    return {'variant': variant, 'input_sha256': VARIANTS[variant]['raw_sha256'],
            'cases': len(traces), 'functions': len(ENTRIES),
            'trace_sha256': hashlib.sha256(json.dumps(traces).encode()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/ui-workflows-tests.json')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-c-workflows-') as directory:
        native = compile_native(Path(directory))
        reports = [verify(variant, native) for variant in VARIANTS]
    result = {'cases': sum(r['cases'] for r in reports), 'variants': reports,
        'source_sha256': {s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest() for s in SOURCES},
        'scope': 'Original ARM instructions and native typed workflow source; exact text, callback identity, menu labels, returns, state and ordered boundary events.',
        'limits': ['Menu input, scrolling-message wait, alarm and hardware services are scripted boundaries.',
                   'Physical appliance not tested; this is not a whole-firmware result.']}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('C workflows:', result['cases'], 'original/native scenarios passed')


if __name__ == '__main__':
    main()
