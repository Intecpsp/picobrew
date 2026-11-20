#!/usr/bin/env python3
"""Compare original Z119 front-panel event routing, timers and Stop behavior."""
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
from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]
OPERATIONS = ['sync', 'dispatch', 'alarm', 'tick', 'timer_start', 'timer_reset']

def case(name, op, **values):
    result = dict(id=name, op=op, core=12, runtime=3, instruction=9, downloading=0,
        done=0, total=0, pending=0, transient=[0, 7000, 0], alarm=[0, 7200000, 0],
        alarm_duration=7200000, clock=1001, clock_step=3, now=1000, argument=0)
    result.update(values); return result

def input_cases():
    cases = [case(f'sync-{state}', 'sync', core=state) for state in list(range(17)) + [255, 0xffffffff]]
    for state in [0, 5, 9, 12, 14, 15]:
        for command in list(range(55)) + [63, 0xffffffff]:
            cases.append(case(f'dispatch-{state}-{command}', 'dispatch', core=state, argument=command))
    for enabled in [0, 1]:
        for active in [0, 1]:
            for now in [0, 12345, 0xfffffff0]:
                cases.append(case(f'alarm-{enabled}-{active}-{now}', 'alarm', argument=enabled, clock=now, alarm=[active, 500, 100]))
    for op in ['timer_start', 'timer_reset']:
        for active in [0, 1]:
            for duration in [1, 7000, 0xffffffff]:
                for now in [0, 1234, 0xfffffff0]:
                    cases.append(case(f'{op}-{active}-{duration}-{now}', op, argument=duration,
                                      clock=now, transient=[active, duration, 55]))
    masks = {0, (1 << 14) - 1}
    for i in range(14):
        masks.add(1 << i)
        for j in range(i + 1, 14): masks.add((1 << i) | (1 << j))
    rng = random.Random(0x109958); masks.update(rng.randrange(1 << 14) for _ in range(160))
    for mask in sorted(masks): cases.append(case(f'priority-{mask}', 'tick', pending=mask, downloading=1, done=15, total=120))
    for runtime in range(5):
        for instruction in [9, 14, 48]:
            cases.append(case(f'still-refresh-{runtime}-{instruction}', 'tick', pending=1 << 9, runtime=runtime, instruction=instruction))
    for state in range(17):
        for now in [999, 1000, 1001]:
            cases.append(case(f'timer-tick-{state}-{now}', 'tick', core=state, now=now,
                              transient=[1, 7000, 1000], alarm=[1, 7200000, 1000]))
    for i, (done, total) in enumerate([(0, 0), (1, 0), (100, -1), (-1, 100), (50, 100), (100, 100),
                                      (150, 100), (0x7fffffff, 4), (-0x80000000, 7)]):
        cases.append(case(f'progress-{i}', 'tick', downloading=1, done=done, total=total))
    cases.append(case('timer-wrap', 'tick', now=0xfffffff0, transient=[1, 55, 10], alarm=[1, 7200000, 15], clock=0xffffffff))
    return cases

def arguments(item):
    return [item['core'], item['runtime'], item['instruction'], item['downloading'], item['done'], item['total'], item['pending'],
            *item['transient'], *item['alarm'], item['alarm_duration'], item['clock'], item['clock_step'], item['now'], item['argument']]

def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++')); path = Path(directory) / 'head_flow.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared', '-I', str(ROOT / 'include'),
        str(ROOT / 'src/head_flow.cpp'), str(ROOT / 'tests/head_flow_bridge.cpp'), '-o', str(path)], check=True)
    native = ctypes.CDLL(str(path)); u32 = ctypes.POINTER(ctypes.c_uint32)
    native.head_flow_test.argtypes = [ctypes.c_uint, u32, u32, u32, ctypes.c_size_t]; native.head_flow_test.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]

def native_case(native, item):
    args = arguments(item); values = (ctypes.c_uint32 * 16)(); events = (ctypes.c_uint32 * 512)()
    result = native.head_flow_test(OPERATIONS.index(item['op']), (ctypes.c_uint32 * len(args))(*(x & 0xffffffff for x in args)), values, events, len(events))
    if result: raise AssertionError(f'Native Head fixture failed {item["id"]}: {result}')
    return dict(id=item['id'], result=values[0], timers=list(values[2:8]), pending=values[8], clocks=values[9],
                events=[list(events[i:i + 4]) for i in range(0, values[1], 4)])

def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--prepare-input', type=Path)
    parser.add_argument('--original-jsonl', type=Path); parser.add_argument('--input', type=Path)
    parser.add_argument('--export', type=Path); parser.add_argument('--write-evidence', action='store_true'); args = parser.parse_args()
    cases = json.loads(args.input.read_text()) if args.input else input_cases()
    if args.write_evidence and args.input: parser.error('Evidence requires complete deterministic cases')
    if args.prepare_input: args.prepare_input.write_text(json.dumps(cases, separators=(',', ':')) + '\n'); print(f'Prepared {len(cases)} Head flow cases')
    if not args.original_jsonl: return 0
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    input_hash = digest(args.input) if args.input else hashlib.sha256((json.dumps(cases, separators=(',', ':')) + '\n').encode()).hexdigest()
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA or execution['input_sha256'] != IMAGE_SHA or execution['input_file_sha256'] != input_hash:
        raise SystemExit('Image or execution input identity mismatch')
    if execution['cases'] != len(cases) or len(originals) != len(cases): raise SystemExit('Original case count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-head-flow-') as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, originals):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(case=item, original=expected, native=actual), indent=2)); raise SystemExit('Head flow mismatch')
    files = ['include/zseries/head_flow.hpp', 'src/head_flow.cpp', 'tests/head_flow_bridge.cpp',
             'tools/validate_head_flow.py', 'tools/ghidra/EmulateHeadFlow.java', 'tools/ghidra/DisplayExecution.java']
    report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases), all_results_match=True, compiler=compiler,
        original_execution_jsonl_sha256=digest(args.original_jsonl), source_sha256={p: digest(ROOT / p) for p in files},
        hardware_tested=False, **{key: execution[key] for key in ['scope', 'original_functions_executed', 'modeled_boundaries']})
    if args.export: report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export: parser.error('--write-evidence requires --export')
        fixture = ROOT / 'tests/fixtures/head-flow-original.json'
        fixture.write_text(json.dumps(dict(input_sha256=IMAGE_SHA, cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]), indent=2) + '\n')
        report['golden_fixture_sha256'] = digest(fixture)
        (ROOT / 'analysis/head-flow-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'All {len(cases)} original/native Head flow cases match')
    return 0
if __name__ == '__main__': raise SystemExit(main())
