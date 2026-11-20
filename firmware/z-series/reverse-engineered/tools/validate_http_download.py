#!/usr/bin/env python3
"""Verify HttpUpdate and DownloadBinary against the original Z119 receiver bodies."""
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
SOURCES = ['include/zseries/http_download.hpp', 'src/http_download.cpp',
    'include/zseries/http_client.hpp', 'include/zseries/console.hpp',
    'include/zseries/diagnostics.hpp', 'src/diagnostics.cpp',
    'tests/http_download_bridge.cpp', 'tools/validate_http_download.py',
    'tools/ghidra/EmulateHttpDownload.java', 'tools/ghidra/Z119Execution.java']


def input_cases():
    result = []
    def add(mode, received=123, expected=1024, started=0xffffff00, completed=0, error=0,
            begin=1, end=1, finished=1, written=0, sdk_error=7, size=0, type='application/octet-stream'):
        index = len(result)
        result.append(dict(input=[mode, received, expected, started, completed, error,
            [0, 16777217, 0xffffffff][index % 3], [0, 0xffffffff][index % 2],
            begin, end, finished, written, sdk_error], type=type, size=size))
    for mode in [0, 3]:
        for expected in [0, 1, 2048, 2049, 0x7fffffff, 0x80000000, 0xffffffff]:
            for type in ['', 'application/octet-stream', 'application/octet-stream; charset=utf-8', 'APPLICATION/OCTET-STREAM']:
                for error in [0, 1]:
                    for begin in [0, 1]:
                        add(mode, expected=expected, type=type, error=error, begin=begin)
    for received in [0, 1, 0xfffffffe]:
        for size in [0, 1, 2, 1024, 2048]:
            for written in [0, 1, size]:
                add(1, received=received, size=size, written=written)
    for end in [0, 1]:
        for finished in [0, 1]:
            for error in [0, 1]:
                for started in [0, 16777217, 0xffffff00]:
                    add(2, end=end, finished=finished, error=error, started=started)
    for received in [0, 1, 2047, 2048]:
        for size in [0, 1, 2, 1024, 2048, 2049]:
            for error in [0, 1]:
                add(4, received=received, size=size, error=error)
        for expected in [received, 0xffffffff, 2049]:
            for error in [0, 1]:
                add(5, received=received, expected=expected, error=error)
    for mode in [6, 7]:
        for size in [0, 1, 1024, 2048, 2049]:
            for expected in [size, 0xffffffff, size + 1]:
                for end in [0, 1]:
                    add(mode, received=0, size=size, expected=expected, end=end, written=size // 2)
    return result


def serialized_inputs():
    return (json.dumps(input_cases(), separators=(',', ':')) + '\n').encode()


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    path = Path(directory) / 'http_download.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fno-fast-math', '-ffp-contract=off', '-shared', '-fPIC', '-I', str(ROOT / 'include'),
        *[str(ROOT / p) for p in ['src/http_download.cpp', 'src/diagnostics.cpp', 'tests/http_download_bridge.cpp']],
        '-o', str(path)], check=True)
    native = ctypes.CDLL(str(path))
    native.http_download_case.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.c_char_p, ctypes.c_uint,
                                         ctypes.POINTER(ctypes.c_uint32)]
    native.http_download_event.argtypes = [ctypes.c_uint]; native.http_download_event.restype = ctypes.c_char_p
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, item):
    output = (ctypes.c_uint32 * 10)()
    native.http_download_case((ctypes.c_uint32 * 13)(*item['input']), item['type'].encode(), item['size'], output)
    return dict(output=list(output), events=[native.http_download_event(i).decode() for i in range(native.http_download_event_count())])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path); parser.add_argument('--original-jsonl', type=Path)
    parser.add_argument('--export', type=Path); parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True, exist_ok=True); args.prepare_input.write_bytes(serialized_inputs())
        print('Prepared %u HTTP download cases' % len(input_cases()))
    if not args.original_jsonl: return
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA or execution['input_sha256'] != IMAGE_SHA:
        raise SystemExit('Original image identity mismatch')
    if execution['input_file_sha256'] != hashlib.sha256(serialized_inputs()).hexdigest():
        raise SystemExit('Original inputs mismatch')
    cases = input_cases()
    if len(cases) != len(originals) or execution['cases'] != len(cases): raise SystemExit('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='http-download-native-') as directory:
        native, compiler = load_native(directory)
        for index, (item, expected) in enumerate(zip(cases, originals)):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(index=index, input=item, actual=actual, original=expected), indent=2))
                raise SystemExit('Original/native HTTP download mismatch')
    if args.write_evidence:
        if not args.export: parser.error('--export is required')
        fixture = ROOT / 'tests/fixtures/http-download-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA,
            cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]), separators=(',', ':')) + '\n').encode(), mtime=0))
        report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases), all_results_match=True,
            hardware_tested=False, compiler=compiler, source_sha256={p: digest(ROOT / p) for p in SOURCES},
            golden_fixture_sha256=digest(fixture), original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{k: execution[k] for k in ['scope', 'original_functions_executed', 'modeled_boundaries']})
        report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json'))
            for a in report['original_functions_executed']}
        (ROOT / 'analysis/http-download-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print('All %u original/native HTTP download cases match' % len(cases))


if __name__ == '__main__': main()
