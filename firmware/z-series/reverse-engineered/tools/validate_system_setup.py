#!/usr/bin/env python3
"""Compare System setup and the concrete assertion helper with Z119 instructions."""
import argparse
import ctypes
import gzip
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATHS = [
    'include/zseries/diagnostics.hpp', 'src/diagnostics.cpp',
    'include/zseries/system_setup.hpp', 'src/system_setup.cpp',
    'tests/system_setup_bridge.cpp', 'tools/validate_system_setup.py',
    'tools/ghidra/EmulateSystemSetup.java', 'tools/ghidra/Z119Execution.java',
]


def input_cases():
    result = []
    for now in [0, 1, 16777217, 0xffffffff]:
        for counter in [0, 0xffffffff]:
            for first in [0, 1, 0x102, 0x80000000, 0xffffffff]:
                for second in [0, 1, 0x102, 0x80000000, 0xffffffff]:
                    i = len(result)
                    handles = [0, 0, 0] if i % 3 == 0 else [0x3f410000, 0x3f410040, 0x3f410080]
                    result.append(dict(input=[now, counter, first, second] + handles))
    return result


def serialized_inputs():
    return (json.dumps(input_cases(), separators=(',', ':')) + '\n').encode()


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    output = Path(directory) / 'system_setup.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fno-fast-math', '-ffp-contract=off', '-shared', '-fPIC', '-I', str(ROOT / 'include'),
        str(ROOT / 'src/diagnostics.cpp'), str(ROOT / 'src/system_setup.cpp'),
        str(ROOT / 'tests/system_setup_bridge.cpp'), '-o', str(output)], check=True)
    native = ctypes.CDLL(str(output))
    native.system_setup_case.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint,
        ctypes.c_uint, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint)]
    native.system_setup_case.restype = ctypes.c_uint
    for name in ['system_console_token', 'system_console_description']:
        function = getattr(native, name)
        function.argtypes = [ctypes.c_uint]
        function.restype = ctypes.c_char_p
    native.system_console_exact.argtypes = [ctypes.c_uint]
    native.system_console_exact.restype = ctypes.c_uint
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, item, missing=0, failure=99):
    data = (ctypes.c_uint32 * 7)(*item['input'])
    output, events, count = (ctypes.c_uint32 * 7)(), (ctypes.c_uint32 * 256)(), ctypes.c_uint()
    result = native.system_setup_case(data, missing, failure, output, events, ctypes.byref(count))
    if count.value > 256:
        raise ValueError('Native event output exceeded its bounded buffer')
    commands = [[native.system_console_exact(i), native.system_console_token(i).decode(),
        native.system_console_description(i).decode()] for i in range(7)]
    return dict(output=list(output), events=list(events)[:count.value], result=result,
                commands=commands, namespaces=['System', 'b '])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path)
    parser.add_argument('--original-jsonl', type=Path)
    parser.add_argument('--export', type=Path)
    parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True, exist_ok=True)
        args.prepare_input.write_bytes(serialized_inputs())
        print('Prepared %u System setup cases' % len(input_cases()))
    if not args.original_jsonl:
        return
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA:
        raise SystemExit('Unexpected original firmware identity')
    if (execution['input_sha256'] != IMAGE_SHA or
            execution['input_file_sha256'] != hashlib.sha256(serialized_inputs()).hexdigest()):
        raise SystemExit('Original execution input identity mismatch')
    cases = input_cases()
    if len(cases) != len(originals) or execution['cases'] != len(cases):
        raise SystemExit('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='z-system-setup-') as directory:
        native, compiler = load_native(directory)
        for index, (item, expected) in enumerate(zip(cases, originals)):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(index=index, input=item, actual=actual, original=expected), indent=2))
                raise SystemExit('Original/native System setup mismatch')
    if args.write_evidence:
        if not args.export:
            parser.error('--export is required to preserve instruction identities')
        fixture = ROOT / 'tests/fixtures/system-setup-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA,
            cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]),
            separators=(',', ':')) + '\n').encode(), mtime=0))
        report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases),
            all_results_match=True, hardware_tested=False, compiler=compiler,
            source_sha256={p: digest(ROOT / p) for p in SOURCE_PATHS},
            golden_fixture_sha256=digest(fixture),
            original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{k: execution[k] for k in ['scope', 'original_functions_executed', 'modeled_boundaries']})
        report['original_instruction_sha256'] = {address: digest(args.export / 'instructions' /
            ('fn_' + address[2:] + '.json')) for address in report['original_functions_executed']}
        (ROOT / 'analysis/system-setup-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print('All %u original/native System setup cases match' % len(cases))


if __name__ == '__main__':
    main()
