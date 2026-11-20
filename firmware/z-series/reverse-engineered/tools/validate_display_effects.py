#!/usr/bin/env python3
"""Replay original Z119 gradients, carets, animation timing and slide effects."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile
from validate_display import IMAGE_SHA, digest, pattern

ROOT = Path(__file__).resolve().parents[1]
OPERATIONS = ['vertical', 'horizontal', 'caret', 'animation', 'slide', 'aligned']

def case(name, op, args, **extra):
    return dict(id=name, op=op, seed=29, args=args, shape=[8, 6, 4], null_mask=0, times=[], **extra)

def input_cases():
    cases = []
    rectangles = [[0, 0, 127, 63], [0, 0, 0, 0], [1, 1, 1, 1], [2, 2, 2, 15],
                  [1, 3, 10, 5], [-10, -3, 13, 4], [117, 59, 145, 80], [-9, 3, -2, 8]]
    for x in range(4):
        for width in range(1, 20):
            rectangles.append([x, 7, x + width - 1, 7 + (width % 17)])
    rng = random.Random(0x0E7150)
    for _ in range(55):
        x, y = rng.randrange(-12, 132), rng.randrange(-8, 68)
        rectangles.append([x, y, x + rng.randrange(1, 33), y + rng.randrange(1, 24)])
    for n, rect in enumerate(rectangles):
        for ascending in [0, 1]: cases.append(case(f'vertical-{n}-{ascending}', 'vertical', rect + [0, ascending]))
        for origin in [0, 1]:
            for descending in [0, 1]: cases.append(case(f'horizontal-{n}-{origin}-{descending}', 'horizontal', rect + [origin, descending]))
    for direction in range(5):
        for height in [2, 3, 4, 8, 16, 32, 65]:
            for n, point in enumerate([[10, 10], [-4, -3], [125, 61]]):
                cases.append(case(f'caret-{direction}-{height}-{n}', 'caret', point + [height, [1, 15, 255][n], direction]))
    for repeats in [0, 1, 2]:
        for frame_count in [1, 2, 3]:
            for mask in [0, 1, 5]:
                item = case(f'animation-{repeats}-{frame_count}-{mask}', 'animation', [10, repeats, frame_count, 0, 0, 10, 1, 1, 3, 4])
                item.update(null_mask=mask, times=[0, 10, 11, 21, 22, 33, 44, 55, 66]); cases.append(item)
    for n, args in enumerate([[20, 0, 3, 0, 1, 0xfffffff0, 1, 0, -3, 60],
                              [20, 0, 3, 0, 2, 0, 0, 1, 0, 0],
                              [0, 0, 3, 0, 1, 0, 1, 0, 0, 0]]):
        item = case(f'animation-special-{n}', 'animation', args); item['times'] = [0xfffffff0, 0xfffffff1, 4, 5, 6]; cases.append(item)
    for direction in range(4):
        for n, target in enumerate([[0, 0], [8, 6]]):
            start = [-8, 128, -6, 64][direction]; goal = target[0 if direction < 2 else 1]
            distance = abs(goal - start)
            item = case(f'slide-{direction}-{n}', 'slide', [0, 0, 0, direction, *target, max(1, distance // 2), 7]); cases.append(item)
        for alignment in [0, 1, 2]:
            target = [(128 - 8) // 2 if alignment == 1 else 120 if alignment == 2 else 0,
                      (64 - 6) // 2 if alignment == 1 else 58 if alignment == 2 else 0]
            distance = abs(target[0 if direction < 2 else 1] - [-8, 128, -6, 64][direction])
            item = case(f'aligned-{direction}-{alignment}', 'aligned', [0, 0, 0, direction, 0, 0, distance, 3, alignment, alignment]); cases.append(item)
    return cases

def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++')); library = Path(directory) / 'display_effects.so'
    files = ['src/display.cpp', 'src/display_effects.cpp', 'tests/display_effects_bridge.cpp']
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared', '-I', str(ROOT / 'include')] + [str(ROOT / p) for p in files] + ['-o', str(library)], check=True)
    native = ctypes.CDLL(str(library)); u32 = ctypes.POINTER(ctypes.c_uint32); u8 = ctypes.POINTER(ctypes.c_uint8)
    native.display_effect_test.argtypes = [ctypes.c_uint, u32, u32, ctypes.c_size_t, u8, u8, u32, u8, ctypes.c_size_t]
    native.display_effect_test.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]

def native_case(native, item):
    args = [0] * 24; args[:len(item['args'])] = item['args']; args[16:19] = item['shape']; args[19] = item['seed']; args[20] = item['null_mask']
    pixels = (ctypes.c_uint8 * 4096).from_buffer_copy(pattern(4096, item['seed'], 73))
    transfer = (ctypes.c_uint8 * 4096).from_buffer_copy(pattern(4096, item['seed'] + 47, 73))
    values = (ctypes.c_uint32 * 16)(); events = (ctypes.c_uint8 * 1048576)(); times = item['times']
    result = native.display_effect_test(OPERATIONS.index(item['op']), (ctypes.c_uint32 * 24)(*(x & 0xffffffff for x in args)),
        (ctypes.c_uint32 * len(times))(*times), len(times), pixels, transfer, values, events, len(events))
    if result: raise AssertionError(f'Native bridge failed {item["id"]}: {result}')
    return dict(id=item['id'], result=values[8], pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),
        transfer_sha256=hashlib.sha256(bytes(transfer)).hexdigest(), events_sha256=hashlib.sha256(bytes(events[:values[0]])).hexdigest(),
        motion=list(values[1:6]), flushes=values[6], delays=values[7])

def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--prepare-input', type=Path)
    parser.add_argument('--original-jsonl', type=Path); parser.add_argument('--input', type=Path)
    parser.add_argument('--export', type=Path); parser.add_argument('--write-evidence', action='store_true'); args = parser.parse_args()
    cases = json.loads(args.input.read_text()) if args.input else input_cases()
    if args.write_evidence and args.input: parser.error('Evidence requires complete deterministic cases')
    if args.prepare_input: args.prepare_input.write_text(json.dumps(cases, separators=(',', ':')) + '\n'); print(f'Prepared {len(cases)} display effect cases')
    if not args.original_jsonl: return 0
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    input_hash = digest(args.input) if args.input else hashlib.sha256((json.dumps(cases, separators=(',', ':')) + '\n').encode()).hexdigest()
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA or execution['input_sha256'] != IMAGE_SHA or execution['input_file_sha256'] != input_hash:
        raise SystemExit('Image or execution input identity mismatch')
    if execution['cases'] != len(cases) or len(originals) != len(cases): raise SystemExit('Original case count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-display-effects-') as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, originals):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(case=item, original=expected, native=actual), indent=2)); raise SystemExit('Display effects mismatch')
    files = ['include/zseries/display_effects.hpp', 'src/display_effects.cpp', 'src/display.cpp', 'tests/display_effects_bridge.cpp',
             'tools/validate_display_effects.py', 'tools/ghidra/EmulateDisplayEffects.java', 'tools/ghidra/DisplayExecution.java']
    report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases), all_results_match=True, compiler=compiler,
        original_execution_jsonl_sha256=digest(args.original_jsonl), source_sha256={p: digest(ROOT / p) for p in files},
        hardware_tested=False, **{key: execution[key] for key in ['scope', 'original_functions_executed', 'modeled_boundaries']})
    if args.export: report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export: parser.error('--write-evidence requires --export')
        fixture = ROOT / 'tests/fixtures/display-effects-original.json'
        fixture.write_text(json.dumps(dict(input_sha256=IMAGE_SHA, cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]), indent=2) + '\n')
        report['golden_fixture_sha256'] = digest(fixture)
        (ROOT / 'analysis/display-effects-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'All {len(cases)} original/native display effect cases match')
    return 0
if __name__ == '__main__': raise SystemExit(main())
