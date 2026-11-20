#!/usr/bin/env python3
"""Compare Z119 console registration/dispatch/help and serial input with original code."""
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
SOURCES = ['include/zseries/console.hpp', 'src/console.cpp',
           'include/zseries/serial_console.hpp', 'src/serial_console.cpp',
           'include/zseries/diagnostics.hpp', 'src/diagnostics.cpp',
           'tests/console_bridge.cpp', 'tools/validate_console.py',
           'tools/ghidra/EmulateConsole.java', 'tools/ghidra/Z119Execution.java']


def input_cases():
    cases = []
    def add(mode, count=0, flag=0, other=0, token='', description='Description', input='',
            stream=b'', errors=0, pending=0, bad=0, truncated=0):
        index = len(cases)
        cases.append(dict(data=[mode, count, flag, other,
            [0, 16777217, 0xffffffff][index % 3], [0, 0xffffffff][index % 2],
            errors, pending, bad, truncated], token=token, description=description,
            input=input, stream=list(stream)))
    for count in [0, 7, 8]:
        for token in [None, '', 'go ', 'x' * 40]:
            for description in [None, '', 'Description']:
                for exact in [0, 1]:
                    add(0, count, exact, token=token, description=description)
            for absent in [0, 1]:
                add(1, count, other=absent, token=token)
    for variant in [0, 1, 2]:
        for command in ['', '?', '??', '???', '?what', 'status', 'status ', 'status extra',
                        'STATUS', 'st', 'stop', 'set', 'set ', 'set   12 ', 'setter',
                        'child', 'child ', 'child run', 'child run ', 'child value  42 ',
                        'child ?', 'child ??', 'child nope', 'child child run']:
            add(2, variant, input=command)
        for recursive in [0, 1]:
            for prefix in ['', 'outer ', 'q' * 126, 'q' * 140]:
                add(3, variant, recursive, token=prefix)
    for initial in ['', 'partial', 'a' * 127, 'a' * 128]:
        for stream in [b'', b'X', b'\n', b'\r\n', b'\0', b'next\nsecond\n', b'\x01ok\n', b'bad\xff\n', b'\x7f\n']:
            for ticks in [1, 3]:
                add(4, other=ticks, token=initial, stream=stream)
    for byte in range(256):
        add(4, other=1, token='prefix', stream=bytes([byte, 10]), errors=0xffffffff)
    for frozen in [0, 1]:
        for initial in ['', 'keep', 'a' * 128]:
            for truncated in [0, 1]:
                add(4, flag=frozen, other=2, token=initial, stream=b'12\nrest\n', truncated=truncated)
    for bad in [0, 1]:
        for pending in [0, 1]:
            add(4, other=3, token='existing', description='first command',
                stream=b'\nnext\nthird\n', pending=pending, bad=bad)
    for pending in [0, 1]:
        for old in ['', 'original', 'o' * 128]:
            for new in ['', 'new command', 'n' * 128]:
                add(5, description=old, input=new, pending=pending)
    return cases


def serialized_inputs():
    return (json.dumps(input_cases(), separators=(',', ':')) + '\n').encode()


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    output = Path(directory) / 'console.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fno-fast-math', '-ffp-contract=off', '-shared', '-fPIC', '-I', str(ROOT / 'include'),
        *[str(ROOT / p) for p in ['src/console.cpp', 'src/serial_console.cpp',
                                 'src/diagnostics.cpp', 'tests/console_bridge.cpp']], '-o', str(output)], check=True)
    native = ctypes.CDLL(str(output))
    native.console_case.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint32)]
    native.console_case.restype = ctypes.c_uint
    for kind in ['event', 'text']:
        getter = getattr(native, 'console_' + kind)
        getter.argtypes = [ctypes.c_uint]; getter.restype = ctypes.c_char_p
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, item):
    data = (ctypes.c_uint32 * 10)(*item['data'])
    output = (ctypes.c_uint32 * 12)()
    stream = (ctypes.c_uint8 * len(item['stream']))(*item['stream'])
    strings = [item[k].encode('latin1') if item[k] is not None else None for k in ['token', 'description', 'input']]
    native.console_case(data, *strings, stream, len(stream), output)
    def collect(kind):
        count = getattr(native, 'console_' + kind + '_count')()
        return [getattr(native, 'console_' + kind)(i).decode('latin1') for i in range(count)]
    return dict(output=list(output), events=collect('event'), texts=collect('text'))


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
        print('Prepared %u console cases' % len(input_cases()))
    if not args.original_jsonl:
        return
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA:
        raise SystemExit('Original firmware identity mismatch')
    if execution['input_sha256'] != IMAGE_SHA or execution['input_file_sha256'] != hashlib.sha256(serialized_inputs()).hexdigest():
        raise SystemExit('Original execution input identity mismatch')
    cases = input_cases()
    if len(cases) != len(originals) or execution['cases'] != len(cases):
        raise SystemExit('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='console-native-') as directory:
        native, compiler = load_native(directory)
        for index, (item, expected) in enumerate(zip(cases, originals)):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(index=index, input=item, actual=actual, original=expected), indent=2))
                raise SystemExit('Console original/native mismatch')
    if args.write_evidence:
        if not args.export:
            parser.error('--export is required')
        fixture = ROOT / 'tests/fixtures/console-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA,
            cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]),
            separators=(',', ':')) + '\n').encode(), mtime=0))
        report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases),
            all_results_match=True, hardware_tested=False, compiler=compiler,
            source_sha256={p: digest(ROOT / p) for p in SOURCES}, golden_fixture_sha256=digest(fixture),
            original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{k: execution[k] for k in ['scope', 'original_functions_executed', 'modeled_boundaries']})
        report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json'))
            for a in report['original_functions_executed']}
        (ROOT / 'analysis/console-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print('All %u original/native console cases match' % len(cases))


if __name__ == '__main__':
    main()
