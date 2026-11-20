#!/usr/bin/env python3
"""Verify client selection and Ethernet transport against the original Z119 bodies."""
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
SOURCES = ['include/zseries/network_client.hpp', 'src/network_client.cpp',
    'include/zseries/http_client.hpp', 'include/zseries/console.hpp',
    'include/zseries/diagnostics.hpp', 'src/diagnostics.cpp',
    'tests/network_client_bridge.cpp', 'tools/validate_network_client.py',
    'tools/ghidra/EmulateNetworkClient.java', 'tools/ghidra/Z119Execution.java']


def input_cases():
    result = []
    def add(mode, **kw):
        v = [mode,8,0,0,0,0,1,0,0,1024,0,1,1,1,0,1,7,0x5a,4,0]
        fields = dict(socket=1, network=1, secure=2, tls=3, kind=3, mask=4, index=4,
                      connected=5, scan=5, connect=6, port=9, status=10, send=11,
                      receive=12, tls_read=13, wants=14, handshake=15, available=16,
                      byte=17, size=18, write_error=19)
        for key, value in kw.items(): v[fields[key]] = value & 0xffffffff
        v[7] = [0, 16777217, 0xfffffff0][len(result) % 3]
        v[8] = [0, 0xffffffff][len(result) % 2]
        result.append(v)
    for network in [0,1,2,3,0xffffffff]:
        for secure in [0,1]:
            for kind in [0,1,2,3,99]:
                for mask in [0,1,2,4,7]:
                    for connected in [0,7]: add(0,network=network,secure=secure,kind=kind,mask=mask,connected=connected)
    for mask in range(8): add(1,mask=mask,kind=99); add(2,mask=mask,kind=99,connected=7)
    for socket in [0,7,8]:
        for index in [0,3,7,8]:
            for scan in [0,0x18,0x1c]:
                for status in [0,1,2,3]: add(3,socket=socket,index=index,scan=scan,status=status)
    for secure in [0,1]:
        for connect in [0,1]:
            for port in [0,1024,65534,65535]:
                for handshake in [1,0,-1,-0x7200]: add(3,secure=secure,connect=connect,port=port,handshake=handshake)
    for socket in [0,7,8]:
        for status in [0,0x13,0x14,0x15,0x17,0x18,0x1a,0x1b,0x1c,0x1d,0x22,255]:
            for available in [0,1,2048]: add(4,socket=socket,status=status,available=available)
    for tls in [0,1]:
        for size in [0,1,64,65535,65536]:
            for send in [0,1,64,-1,-0x7200]:
                for socket in [0,8]: add(5,tls=tls,size=size,send=send,socket=socket,wants=2 if tls else 0)
            for receive in [0,1,64,-1,-0x6900]: add(6,tls=tls,size=size,receive=receive,tls_read=receive,socket=0)
        for available in [0,1,2048]:
            for tls_read in [0,1,-1,-0x6900,-0x6880,-0x7200]:
                for mode in [7,8]: add(mode,tls=tls,available=available,tls_read=tls_read,socket=0)
        for receive in [0,1,-1]: add(10,tls=tls,receive=receive,tls_read=receive,socket=0)
    for socket in [0,7,8]:
        for tls in [0,1]:
            for status in [0,1,10,2500]: add(9,socket=socket,tls=tls,status=status)
        add(11,socket=socket)
    return result


def serialized_inputs():
    return (json.dumps(input_cases(), separators=(',', ':')) + '\n').encode()


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    path = Path(directory) / 'network_client.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fno-fast-math', '-ffp-contract=off', '-shared', '-fPIC', '-I', str(ROOT / 'include'),
        *[str(ROOT / p) for p in ['src/network_client.cpp', 'src/diagnostics.cpp', 'tests/network_client_bridge.cpp']],
        '-o', str(path)], check=True)
    native = ctypes.CDLL(str(path))
    native.network_client_case.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
    native.network_client_event.argtypes = [ctypes.c_uint]; native.network_client_event.restype = ctypes.c_char_p
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, item):
    output = (ctypes.c_uint32 * 17)()
    native.network_client_case((ctypes.c_uint32 * 20)(*item), output)
    return dict(output=list(output), events=[native.network_client_event(i).decode() for i in range(native.network_client_event_count())])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path); parser.add_argument('--original-jsonl', type=Path)
    parser.add_argument('--export', type=Path); parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True, exist_ok=True); args.prepare_input.write_bytes(serialized_inputs())
        print('Prepared %u network client cases' % len(input_cases()))
    if not args.original_jsonl: return
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA or execution['input_sha256'] != IMAGE_SHA:
        raise SystemExit('Original image identity mismatch')
    if execution['input_file_sha256'] != hashlib.sha256(serialized_inputs()).hexdigest():
        raise SystemExit('Original inputs mismatch')
    cases = input_cases()
    if len(cases) != len(originals) or execution['cases'] != len(cases): raise SystemExit('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='network-client-native-') as directory:
        native, compiler = load_native(directory)
        for index, (item, expected) in enumerate(zip(cases, originals)):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(index=index, input=item, actual=actual, original=expected), indent=2))
                raise SystemExit('Original/native network client mismatch')
    if args.write_evidence:
        if not args.export: parser.error('--export is required')
        fixture = ROOT / 'tests/fixtures/network-client-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA,
            cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]), separators=(',', ':')) + '\n').encode(), mtime=0))
        report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases), all_results_match=True,
            hardware_tested=False, compiler=compiler, source_sha256={p: digest(ROOT / p) for p in SOURCES},
            golden_fixture_sha256=digest(fixture), original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{k: execution[k] for k in ['scope', 'original_functions_executed', 'modeled_boundaries']})
        report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json'))
            for a in report['original_functions_executed']}
        (ROOT / 'analysis/network-client-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print('All %u original/native network client cases match' % len(cases))


if __name__ == '__main__': main()
