#!/usr/bin/env python3
"""Compare physical input producers with original Z119 instruction execution."""
import argparse
import ctypes
import gzip
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = 'd84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
OPERATIONS = ['button_setup', 'button_tick', 'button_edge', 'button_state', 'button_emit',
              'encoder_setup', 'encoder_sample', 'encoder_interrupt', 'encoder_tick',
              'encoder_emit', 'console_press', 'console_rotate', 'button_interrupt']
WORDS = ['pin', 'id', 'pressed', 'count', 'state', 'date', 'pending', 'queue_id', 'queue_long', 'queue_count',
         'pin_a', 'pin_b', 'pin_button', 'activity', 'phase', 'previous', 'write', 'read', 'total',
         'button_assertion', 'encoder_assertion', 'clock']

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def case(name, steps, reads=(), ring=None, **changes):
    values = dict(pin=39, id=1, pressed=0, count=0, state=0, date=100, pending=0,
                  queue_id=91, queue_long=1, queue_count=73, pin_a=34, pin_b=35, pin_button=39,
                  activity=17, phase=4, previous=4, write=0, read=0, total=123,
                  button_assertion=0, encoder_assertion=0, clock=1000)
    values.update(changes)
    return dict(id=name, words=[values[k] & 0xffffffff for k in WORDS],
                ring=list(ring) if ring is not None else [0] * 256, reads=list(reads),
                steps=[[OPERATIONS.index(s[0]), *(list(s[1:]) + [0, 0])[:2]] for s in steps])

def input_cases():
    rows = []
    for pin, level in itertools.product((-1, -2147483648, 0, 32, 39, 255, 256, 511), (-1, 0, 1, 2)):
        rows.append(case(f'button-setup-{pin}-{level}', [('button_setup',)], [level], pin=pin, pressed=1))
    for old, new, count in itertools.product(range(5), range(5), (0, 1, 0xffffffff)):
        rows.append(case(f'button-state-{old}-{new}-{count}', [('button_state', new)], state=old, count=count))
    for state, pressed in itertools.product(range(5), (0, 1)):
        rows.append(case(f'button-edge-{state}-{pressed}', [('button_edge', pressed)], state=state, count=2))
    for state, pressed, level, elapsed in itertools.product(range(4), (0, 1), (0, 1), (0, 99, 100, 101, 4999, 5000, 5001)):
        rows.append(case(f'button-tick-{state}-{pressed}-{level}-{elapsed}', [('button_tick', 100 + elapsed)],
                         [level], state=state, pressed=pressed, count=3, clock=100))
    for date, state, now in itertools.product((0, 0xffffffff, 0xfffffff0, 0xffffec78), (1, 2), (0, 99, 100, 5000, 0xffffffff)):
        rows.append(case(f'button-wrap-{date}-{state}-{now}', [('button_tick', now)], [0 if state == 1 else 1],
                         date=date, state=state, pressed=state == 1, count=0xffffffff, clock=0xffffffff))
    for pending, long_press, count, ident in itertools.product((0, 1), (0, 1), (0, 1, 2, 0xffffffff), (0, 1, 127)):
        rows.append(case(f'button-emit-{pending}-{long_press}-{count}-{ident}', [('button_emit', long_press, count)], pending=pending, id=ident))
    rows += [
        case('short-press-release', [('button_tick', 1000), ('button_tick', 1050), ('button_tick', 1102)], [0, 1, 1]),
        case('held-long-once', [('button_tick', 1000), ('button_tick', 6000), ('button_tick', 6001), ('button_tick', 12000), ('button_tick', 12001)], [0, 0, 0, 0, 1]),
        case('multiple-press-count', [('button_tick', 1000), ('button_tick', 1001), ('button_tick', 1002), ('button_tick', 1003), ('button_tick', 1104)], [0, 1, 0, 1, 1]),
        case('occupied-button-retained', [('button_tick', 6001)], [0], pending=1, state=1, pressed=1, date=1000, count=7),
        case('disabled-button-tick', [('button_tick', 0xffffffff)], [0], pin=-1, state=1, pressed=1, count=7),
    ]
    for a, b in itertools.product((-1, 0, 1, 2, 127, 255), repeat=2):
        rows.append(case(f'sample-{a}-{b}', [('encoder_sample',)], [a, b]))
    for pin, click, a, b in itertools.product((0, 34, 39, 40, 255), (0, 1), (0, 1), (0, 1)):
        rows.append(case(f'encoder-setup-{pin}-{click}-{a}-{b}', [('encoder_setup',)], [click, a, b],
                         pin=pin, pin_a=pin, pin_b=pin, pin_button=pin, phase=7, previous=6))
    rows.append(case('encoder-setup-invalid-gpio-value', [('encoder_setup',)], [1, 2, 2]))
    for write, read, value in itertools.product((0, 1, 254, 255), (0, 1, 254, 255), range(4)):
        rows.append(case(f'isr-{write}-{read}-{value}', [('encoder_interrupt',)], [value >> 1, value & 1],
                         write=write, read=read, ring=[(i + 1) & 255 for i in range(256)]))
    for count in (0, 1, 2, 255, 256, 257, 300, 512):
        values = [i % 4 for i in range(count)]
        reads = [v for sample in values for v in (sample >> 1, sample & 1)] + [1]
        rows.append(case(f'ring-overwrite-{count}', [('encoder_interrupt',)] * count + [('encoder_tick', 900)], reads))
    for phase, previous, sample in itertools.product(range(1, 8), range(9), range(4)):
        ring = [0] * 256; ring[255] = sample
        rows.append(case(f'decode-{phase}-{previous}-{sample}', [('encoder_tick', 900)], [1], ring,
                         phase=phase, previous=previous, read=255, write=0))
    traces = [[], [1, 0, 2, 3], [2, 0, 1, 3], [1, 1, 0, 0, 2, 2, 3, 3], [1, 0, 1, 3],
              [2, 0, 2, 3], [0, 3, 0, 3], [1, 0, 2, 3, 2, 0, 1, 3]]
    for index, values in enumerate(traces):
        for start, total in itertools.product((0, 250), (0, 0x7fffffff, 0x80000000, 0xffffffff)):
            ring = [0] * 256
            for i, value in enumerate(values): ring[(start + i) & 255] = value
            rows.append(case(f'trace-{index}-{start}-{total}', [('encoder_tick', 900)], [1], ring,
                             read=start, write=(start + len(values)) & 255, total=total))
    for pending, long_press, count in itertools.product((0, 1), (0, 1), (0, 1, 3)):
        rows.append(case(f'forward-{pending}-{long_press}-{count}', [('encoder_tick', 900)], [1],
                         pending=pending, queue_id=1, queue_long=long_press, queue_count=count))
    for delta in (-2147483648, -4, -3, -2, -1, 0, 1, 2, 3, 4, 2147483647):
        rows.append(case(f'emit-rotation-{delta}', [('encoder_emit', delta)]))
    for delta in (-4, -3, -2, -1, 1, 2, 3, 4):
        rows.append(case(f'console-rotation-{delta}', [('console_rotate', delta)]))
    rows.append(case('console-press-then-tick', [('console_press',), ('encoder_tick', 1000)], [1]))
    rows.append(case('empty-button-isr', [('button_interrupt',)], pending=1))
    rng = random.Random(0x400f0804)
    for i in range(128):
        start, count = rng.randrange(256), rng.randrange(256)
        ring = [rng.randrange(4) for _ in range(256)]
        rows.append(case(f'random-{i}', [('encoder_tick', rng.randrange(2**32))], [rng.randrange(2)], ring,
                         read=start, write=(start + count) & 255, phase=rng.randrange(1, 8), previous=rng.randrange(1, 8),
                         total=rng.randrange(2**32), pressed=rng.randrange(2), state=rng.randrange(3),
                         count=rng.randrange(4), date=rng.randrange(2**32), clock=rng.randrange(2**32), pending=rng.randrange(2)))
    return rows

def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++')); path = Path(directory) / 'physical_inputs.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared', '-I', str(ROOT / 'include'),
                   str(ROOT / 'src/physical_inputs.cpp'), str(ROOT / 'tests/physical_inputs_bridge.cpp'), '-o', str(path)], check=True)
    native = ctypes.CDLL(str(path)); u32 = ctypes.POINTER(ctypes.c_uint32)
    native.physical_inputs_test.argtypes = [u32, ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_int32), ctypes.c_size_t,
                                          u32, ctypes.c_size_t, u32, ctypes.c_size_t]
    native.physical_inputs_test.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]

def native_case(native, item):
    words = (ctypes.c_uint32 * len(WORDS))(*item['words']); ring = (ctypes.c_uint8 * 256)(*item['ring'])
    reads = (ctypes.c_int32 * len(item['reads']))(*item['reads']); steps = [v & 0xffffffff for s in item['steps'] for v in s]
    operations = (ctypes.c_uint32 * len(steps))(*steps); output = (ctypes.c_uint32 * 20000)()
    status = native.physical_inputs_test(words, ring, reads, len(reads), operations, len(item['steps']), output, len(output))
    if status: raise AssertionError(f'Native fixture failed {item["id"]}: {status}')
    at = 24 + 256; count = output[at]; at += 1; results = list(output[at:at + count]); at += count
    event_count = output[at]; at += 1; events = []
    for _ in range(event_count):
        count = output[at]; at += 1; events.append(list(output[at:at + count])); at += count
    if at != output[0] + 1: raise AssertionError('Bad fixture shape')
    return dict(id=item['id'], words=list(output[1:24]), ring=list(output[24:280]), results=results, events=events)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path); parser.add_argument('--original-jsonl', type=Path)
    parser.add_argument('--export', type=Path); parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args(); cases = input_cases(); serialized = json.dumps(cases, separators=(',', ':')) + '\n'
    if args.prepare_input: args.prepare_input.write_text(serialized); print(f'Prepared {len(cases)} physical input cases')
    if not args.original_jsonl: return
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    input_hash = hashlib.sha256(serialized.encode()).hexdigest()
    if execution['input_sha256'] != IMAGE_SHA or digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA:
        raise SystemExit('Original image mismatch')
    if execution['input_file_sha256'] != input_hash or execution['cases'] != len(cases) or len(originals) != len(cases):
        raise SystemExit('Original input/count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-physical-inputs-') as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, originals):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(case=item, original=expected, native=actual), indent=2))
                raise SystemExit('Physical input mismatch')
    paths = ['include/zseries/physical_inputs.hpp', 'src/physical_inputs.cpp', 'platform/esp32/physical_input_platform.hpp',
             'platform/esp32/physical_input_platform.cpp', 'tests/physical_inputs_bridge.cpp', 'tests/test_physical_inputs.py',
             'tools/validate_physical_inputs.py', 'tools/ghidra/EmulatePhysicalInputs.java', 'tools/ghidra/Z119Execution.java']
    report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases), all_results_match=True, compiler=compiler,
                  hardware_tested=False, original_execution_jsonl_sha256=digest(args.original_jsonl), input_cases_sha256=input_hash,
                  source_sha256={p: digest(ROOT / p) for p in paths},
                  **{k: execution[k] for k in ['scope', 'original_functions_executed', 'modeled_boundaries']})
    if args.export:
        report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json'))
                                                 for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export: parser.error('Evidence requires --export')
        fixture = ROOT / 'tests/fixtures/physical-inputs-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA, cases=[dict(input=i, original=o)
            for i, o in zip(cases, originals)]), separators=(',', ':')) + '\n').encode(), mtime=0))
        report['golden_cases'] = len(cases); report['golden_fixture_sha256'] = digest(fixture)
        (ROOT / 'analysis/physical-inputs-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'All {len(cases)} original/native physical input cases match')

if __name__ == '__main__': main()
