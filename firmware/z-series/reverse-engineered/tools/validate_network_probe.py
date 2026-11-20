#!/usr/bin/env python3
"""Replay original Z119 connectivity checks against the real portable probe."""
import argparse
import ctypes
import gzip
import hashlib
import io
import itertools
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]
OWNED = ['include/zseries/network_probe.hpp', 'src/network_probe.cpp',
         'platform/esp32/network_probe_platform.hpp', 'platform/esp32/network_probe_platform.cpp',
         'tests/network_probe_bridge.cpp', 'tools/validate_network_probe.py',
         'tools/ghidra/EmulateNetworkProbe.java', 'tools/ghidra/Z119Execution.java']


def input_cases():
    rows = []
    def add(network, status=0, acquired=1, pointer=1, connection=1):
        rows.append(dict(id=f'network-probe-{len(rows)}', words=[
            network & 0xffffffff, status & 0xffffffff, acquired, pointer, connection & 0xffffffff, 0, 0]))
    for status in list(range(261)) + [-1, -2147483648, 2147483647]:
        add(2, status)
    for acquired, pointer, connection in itertools.product([0, 1], [0, 1], [-33, -1, 0, 1, 2, 2147483647]):
        add(1, acquired=acquired, pointer=pointer, connection=connection)
    for network in [0, 3, 4, 255, 0xffffffff]:
        add(network)
    return rows


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    output = Path(directory) / 'network-probe.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-shared', '-fPIC', '-I', str(ROOT / 'include'), str(ROOT / 'src/network_probe.cpp'),
        str(ROOT / 'tests/network_probe_bridge.cpp'), '-o', str(output)], check=True)
    native = ctypes.CDLL(str(output))
    native.network_probe_case.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint, ctypes.c_uint]
    native.network_probe_case.restype = ctypes.c_uint
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, case, missing=0, fail=0):
    words = (ctypes.c_uint32 * 7)(*case['words'])
    effects = (ctypes.c_uint32 * 64)()
    count = native.network_probe_case(words, effects, missing, fail)
    return dict(id=case['id'], words=list(words), effects=list(effects)[:count])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path)
    parser.add_argument('--original-jsonl', type=Path)
    parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    cases = input_cases()
    serialized = (json.dumps(cases, separators=(',', ':')) + '\n').encode()
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True, exist_ok=True)
        args.prepare_input.write_bytes(serialized)
        print(f'Prepared {len(cases)} network probes')
    if not args.original_jsonl:
        return
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA:
        raise ValueError('Original image changed')
    if execution['input_sha256'] != IMAGE_SHA or execution['input_file_sha256'] != hashlib.sha256(serialized).hexdigest():
        raise ValueError('Original execution/input identity mismatch')
    if len(originals) != len(cases) or execution['cases'] != len(cases):
        raise ValueError('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='network-probe-') as directory:
        native, compiler = load_native(directory)
        for case, original in zip(cases, originals):
            actual = native_case(native, case)
            if actual != original:
                raise ValueError(f"Mismatch {case['id']}: {actual} != {original}")
    if args.write_evidence:
        fixture = dict(schema_version=1, input_sha256=IMAGE_SHA,
                       cases=[dict(input=case, original=result) for case, result in zip(cases, originals)])
        compressed = io.BytesIO()
        with gzip.GzipFile(filename='', fileobj=compressed, mode='wb', mtime=0) as output:
            output.write((json.dumps(fixture, separators=(',', ':')) + '\n').encode())
        path = ROOT / 'tests/fixtures/network-probe-original.json.gz'
        path.write_bytes(compressed.getvalue())
        report = dict(execution, schema_version=1, compiler=compiler, all_results_match=True,
                      complete_application_linked=False, golden_fixture_sha256=digest(path),
                      source_sha256={name: digest(ROOT / name) for name in OWNED})
        (ROOT / 'analysis/network-probe-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'All {len(cases)} full original network probe executions match')


if __name__ == '__main__':
    main()
