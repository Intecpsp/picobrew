#!/usr/bin/env python3
"""Compare System watchdog and loop timing with original Z119 execution."""
import argparse
import ctypes
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import tempfile
from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]


def bits(value):
    return struct.pack('>d', value).hex()


def input_cases():
    cases = []

    def add(name, words, average, ticks):
        cases.append(dict(id=name, words=words, average_bits=bits(average), ticks=ticks,
                          assertions=0xffffffff if len(cases) % 2 else 17))

    for gap in [0, 1, 2, 100, 2999, 3000, 3001, 3002, 10000, 0x7fffffff, 0xffffffff]:
        for maximum in [0, gap, min(0xffffffff, gap + 1)]:
            add('gap-%u-max-%u' % (gap, maximum), [0, 17, 0xffffffff, maximum], 123.5,
                [[gap, 0, 0xfedcba98]])
    for error in [-1, 1, 0x102, -2147483648, 2147483647]:
        add('watchdog-%d' % error, [123, 17, 6, 120], 42.75, [[122, error, 999]])
    for before, now in [(1, 0), (0xffffffff, 0), (0xfffffffe, 1), (0x80000000, 0x7fffffff)]:
        add('regression-%u-%u' % (before, now), [before, 22, 4, 5000], 250.0, [[now, 0, 100]])
    add('default-sequence', [0, 0, 0xffffffff, 0], 0.0,
        [[n, 0, n + 3] for n in [0, 10, 10, 30, 3030, 6031, 9032, 13000, 17000]])
    for average in [-0.0, 0.0, -10.25, 1e-250, 1e250]:
        add('average-' + bits(average), [0, 123, 0xffffffff, 0], average, [[3001, 0, 3010]])
    rng = random.Random(1192020)
    for index in range(180):
        last = rng.randrange(0, 2**31)
        ticks = []
        now = last
        for _ in range(1 + index % 5):
            now += rng.choice([0, 1, 20, 100, 2999, 3000, 3001, 4000, 10000])
            ticks.append([now, 0, rng.randrange(0, 2**32)])
        add('seeded-%u' % index, [last, rng.randrange(10000), rng.randrange(10000), rng.randrange(10000)],
            rng.uniform(-100, 10000), ticks)
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    output = Path(directory) / 'system_tick.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fno-fast-math', '-ffp-contract=off', '-shared', '-fPIC', '-I', str(ROOT / 'include'),
        str(ROOT / 'src/system_tick.cpp'), str(ROOT / 'tests/system_tick_bridge.cpp'), '-o', str(output)], check=True)
    native = ctypes.CDLL(str(output))
    native.system_tick_case.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint64),
        ctypes.c_uint32, ctypes.c_int32, ctypes.c_uint32, ctypes.c_uint, ctypes.c_bool, ctypes.c_bool,
        ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_int32)]
    native.system_tick_case.restype = ctypes.c_uint
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, item, missing=0, log_success=True, assertion_success=True):
    words = (ctypes.c_uint32 * 4)(*item['words'])
    average = ctypes.c_uint64(int(item['average_bits'], 16))
    assertions = ctypes.c_uint32(item['assertions'])
    snapshots = []
    for now, watchdog, timestamp in item['ticks']:
        events = (ctypes.c_uint32 * 64)()
        count, error = ctypes.c_uint(), ctypes.c_int32(0x1234)
        result = native.system_tick_case(words, ctypes.byref(average), now, watchdog, timestamp,
            missing, log_success, assertion_success, ctypes.byref(assertions), events,
            ctypes.byref(count), ctypes.byref(error))
        snapshots.append(dict(words=list(words), average_bits='%016x' % average.value, result=result,
            watchdog_error=error.value, assertions=assertions.value, events=list(events)[:count.value]))
    return dict(id=item['id'], snapshots=snapshots)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path)
    parser.add_argument('--original-jsonl', type=Path)
    parser.add_argument('--export', type=Path)
    parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    cases = input_cases()
    serialized = (json.dumps(cases, separators=(',', ':')) + '\n').encode()
    if args.prepare_input:
        args.prepare_input.write_bytes(serialized)
        print('Prepared %u System tick cases' % len(cases))
    if not args.original_jsonl:
        return
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA:
        raise SystemExit('Unexpected original image')
    if execution['input_sha256'] != IMAGE_SHA or execution['input_file_sha256'] != hashlib.sha256(serialized).hexdigest():
        raise SystemExit('Original input identity mismatch')
    if len(originals) != len(cases) or execution['cases'] != len(cases):
        raise SystemExit('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='z-system-tick-') as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, originals):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(id=item['id'], original=expected, native=actual), indent=2))
                raise SystemExit('System tick mismatch')
    if args.write_evidence:
        if not args.export:
            parser.error('Instruction export required')
        fixture = ROOT / 'tests/fixtures/system-tick-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA,
            cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]),
            separators=(',', ':')) + '\n').encode(), mtime=0))
        paths = ['include/zseries/system_tick.hpp', 'src/system_tick.cpp', 'tests/system_tick_bridge.cpp',
            'tools/validate_system_tick.py', 'tools/ghidra/EmulateSystemTick.java', 'tools/ghidra/Z119Execution.java']
        report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases),
            ticks=sum(len(i['ticks']) for i in cases), all_results_match=True, hardware_tested=False,
            compiler=compiler, source_sha256={p: digest(ROOT / p) for p in paths},
            golden_fixture_sha256=digest(fixture), original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{k: execution[k] for k in ['scope', 'original_functions_executed', 'modeled_boundaries']})
        report['original_instruction_sha256'] = {
            address: digest(args.export / 'instructions' / ('fn_' + address[2:] + '.json'))
            for address in report['original_functions_executed']}
        (ROOT / 'analysis/system-tick-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print('All %u original/native System tick cases match' % len(cases))


if __name__ == '__main__':
    main()
