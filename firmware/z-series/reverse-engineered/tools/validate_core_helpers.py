#!/usr/bin/env python3
"""Compare reconstructed Core helper services with completed Z119 instruction execution."""
import argparse
import ctypes
import gzip
import hashlib
import io
import itertools
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile
from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]


def input_cases():
    cases = []

    def add(kind, values):
        words = [0] * 18
        words[:5] = [kind, 100, 0xffffff80 if len(cases) % 7 == 0 else 12345,
                     0xffffffff if len(cases) % 2 else 17, 0]
        for key, value in values.items():
            words[key] = int(value) & 0xffffffff
        cases.append(dict(id=f'helper-{len(cases)}', words=words))

    for enabled, pending, ready, registered, expired in itertools.product(range(2), repeat=5):
        for now, deadline in [(0, 0), (100, 99), (100, 100), (100, 101), (0, 0xffffffff), (0xffffffff, 0)]:
            add(0, {1: now, 5: enabled, 6: pending, 7: deadline, 8: 0xfffffff0,
                    9: ready, 10: registered, 11: expired})
    for requested, active, complete, error, ready in itertools.product(range(2), repeat=5):
        add(1, {5: requested, 6: active, 7: complete, 8: error, 9: ready})
    for state, busy, stop, ready in itertools.product(range(7), range(2), range(2), range(2)):
        for now, deadline in [(0, 0), (100, 99), (100, 100), (100, 101), (0, 0xffffffff), (0xffffffff, 0)]:
            for created, current in [(0, 0), (42, 42), (42, 7), (42, 0)]:
                add(2, {1: now, 5: state, 6: 111, 7: deadline, 8: 1, 9: stop, 10: 1,
                        11: 0xfeed, 12: busy, 13: created, 14: current, 15: 987, 16: ready})
    for state, next_state, ready in itertools.product(range(7), range(7), range(2)):
        add(3, {5: state, 6: 111, 7: 999, 8: 1, 9: 1, 10: 1, 11: 0xfeed,
                13: 42, 14: 42, 15: 987, 16: ready, 17: next_state})
    for state, resume, ready, error in itertools.product(range(7), range(2), range(2), range(2)):
        add(4, {5: state, 6: 111, 7: 999, 8: error, 9: 1, 10: resume,
                11: 0xfeed, 13: 42, 14: 42, 15: 987, 16: ready})
    rng = random.Random(119391)
    for _ in range(120):
        add(rng.randrange(5), {1: rng.randrange(2**32), 5: rng.randrange(5), 6: 1,
            7: rng.randrange(2**32), 8: 1, 9: 1, 10: 1, 11: 1, 12: rng.randrange(2),
            13: rng.randrange(2**32), 14: rng.randrange(2**32), 15: rng.randrange(2**32),
            16: rng.randrange(2), 17: rng.randrange(5)})
    # Boolean source fields are normalized independently of their ABI storage.
    for case in cases:
        w = case['words']
        for i in ([5, 6, 9, 10, 11] if w[0] == 0 else [5, 6, 7, 8, 9] if w[0] == 1 else [8, 9, 10, 16]):
            w[i] = int(bool(w[i]))
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    output = Path(directory) / 'core_helpers.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-shared', '-fPIC', '-I', str(ROOT / 'include'), str(ROOT / 'src/core_helpers.cpp'),
        str(ROOT / 'tests/core_helpers_bridge.cpp'), '-o', str(output)], check=True)
    native = ctypes.CDLL(str(output))
    native.core_helpers_case.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint), ctypes.c_uint, ctypes.c_uint]
    native.core_helpers_case.restype = ctypes.c_uint
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, case, missing=0, failure=0):
    words = (ctypes.c_uint32 * 18)(*case['words'])
    effects = (ctypes.c_uint32 * 256)()
    count = ctypes.c_uint()
    native.core_helpers_case(words, effects, ctypes.byref(count), missing, failure)
    return dict(id=case['id'], words=list(words), effects=list(effects)[:count.value])


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
        print(f'Prepared {len(cases)} helper cases')
    if not args.original_jsonl:
        return
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA:
        raise ValueError('Original image differs')
    if execution['input_sha256'] != IMAGE_SHA or execution['input_file_sha256'] != hashlib.sha256(serialized).hexdigest():
        raise ValueError('Original input identity differs')
    if len(originals) != len(cases) or execution['cases'] != len(cases):
        raise ValueError('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='z-core-helpers-') as directory:
        native, compiler = load_native(directory)
        for case, expected in zip(cases, originals):
            actual = native_case(native, case)
            if actual != expected:
                print(json.dumps(dict(input=case, original=expected, native=actual), indent=2))
                raise ValueError('Original/native helper mismatch')
    if args.write_evidence:
        if not args.export:
            parser.error('--export is required when writing evidence')
        output = io.BytesIO()
        with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as compressed:
            compressed.write((json.dumps(dict(input_sha256=IMAGE_SHA,
                cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]), separators=(',', ':')) + '\n').encode())
        fixture = ROOT / 'tests/fixtures/core-helpers-original.json.gz'
        fixture.write_bytes(output.getvalue())
        paths = ['include/zseries/core_helpers.hpp', 'src/core_helpers.cpp', 'tests/core_helpers_bridge.cpp',
                 'tools/validate_core_helpers.py', 'tools/ghidra/EmulateCoreHelpers.java', 'tools/ghidra/Z119Execution.java']
        report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases), all_results_match=True,
            hardware_tested=False, complete_application_linked=False, compiler=compiler,
            source_sha256={p: digest(ROOT / p) for p in paths}, golden_fixture_sha256=digest(fixture),
            original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{k: execution[k] for k in ['scope', 'original_functions_executed', 'modeled_boundaries']})
        report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json'))
            for a in report['original_functions_executed']}
        (ROOT / 'analysis/core-helpers-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'All {len(cases)} original/native helper scenarios match')


if __name__ == '__main__':
    main()
