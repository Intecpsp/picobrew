#!/usr/bin/env python3
"""Compare readable local HTTP reconstruction with original Z119 instructions."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]
OPERATIONS = ['parse', 'request', 'reset', 'route']


def case(name, op='parse', lines=None, initial=None, path='', debug=0):
    return dict(id=name, op=op, lines=lines if lines is not None else [None],
                initial=initial or [0, 0, 0, 0, 0, 0], path=path, debug=debug)


def input_cases():
    cases = []
    methods = ['Unknown', 'GET', 'POST', 'PUT', 'DELETE', 'UPDATE', 'HEAD', 'OPTIONS',
               'PATCH', 'get', 'post', 'GETTING', 'POSTSCRIPT', 'UPDATEALL', '', 'X']
    for method in methods:
        for suffix in [' /state HTTP/1.0', ' /ping ignored', '  /state HTTP/1.1', ' /state', '\t/state HTTP/1.1']:
            cases.append(case('request-%u' % len(cases), 'request', [method + suffix],
                              [4, 123, 1, 1, 1, 1], 'previous'))
    for length in [0, 1, 2, 63, 64, 65, 80, 256, 2047]:
        path = '/' * length
        cases.append(case('path-capacity-%u' % length, lines=['GET ' + path + ' HTTP/1.1', '']))
    for debug in [0, 1]:
        for line in [None, '', 'GET', 'GET /ping HTTP/1.0', 'Content-Length: 4']:
            cases.append(case('first-line-%u-%u' % (debug, len(cases)), lines=[line], debug=debug))
    headers = ['Content-Type: application/json', 'Content-Type: application/json; charset=utf-8',
        'Content-Type: application/jsonsuffix', 'content-type: application/json',
        'CONTENT-TYPE: application/json', 'Content-Type:application/json',
        'Content-Type: text/plain', 'Content-Type: Application/JSON', 'X-Content-Type: application/json',
        'Content-Length:', 'Content-Length:4', 'content-length: 4', 'CONTENT-LENGTH: 4',
        'Content-Length : 4', 'Transfer-Encoding: chunked', 'X: y']
    for value in ['', '0', '1', '2047', '2048', '-1', '-2147483648', '2147483647',
                  '+42', ' 123', '\t\r\n 27tail', '0012', '12.5', 'not-a-number', '- 4']:
        headers.append('Content-Length: ' + value)
    for index, line in enumerate(headers):
        for debug in [0, 1]:
            cases.append(case('header-%u-%u' % (index, debug), lines=[line],
                              initial=[2, 55, 0, 1, 0, 0], path='/recipe', debug=debug))
    cases.extend([
        case('complete-json', lines=['POST /recipe HTTP/1.1', 'Host: z.local',
             'Content-Type: application/json', 'Content-Length: 32', '']),
        case('duplicate-headers', lines=['PUT /recipe HTTP/1.0', 'Content-Length: 10',
             'Content-Length: 7', 'Content-Type: application/json', 'Content-Type: text/plain', '']),
        case('empty-before-first', lines=['', 'GET /ping HTTP/1.1', 'Content-Length: 3', '']),
        case('lines-after-done', lines=['GET /state HTTP/1.1', '', 'Content-Length: 4',
             'Content-Type: application/json', 'GET /else HTTP/1.1']),
    ])
    for seen in [0, 1]:
        for done in [0, 1]:
            for overflow in [0, 1]:
                cases.append(case('reset-%u-%u-%u' % (seen, done, overflow), 'reset',
                                  initial=[5, 0xffffffff, 1, seen, done, overflow], path='x' * 64))
    paths = ['', '/', '/ping', '/state', '/help.html', '/lib.js', '/z.css', '/z.html', '/z.js',
             '/zstate.css', '/zstate.html', '/zstate.js', '/recipe', '/missing', '/PING',
             '/state?x=1', '/z.html?x=1', '/help.HTML', '//help.html', 'help.html',
             'xhelp.html', 'xlib.js', '/%7a.html', '/' + 'x' * 63]
    for method in [0, 1, 2, 3, 4, 5, 0xffffffff]:
        for index, path in enumerate(paths):
            cases.append(case('route-%u-%u' % (method, index), 'route',
                              initial=[method, 3, 1, 1, 1, 0], path=path))
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    path = Path(directory) / 'local_http.so'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fPIC', '-shared', '-I', str(ROOT / 'include'), str(ROOT / 'src/local_http.cpp'),
        str(ROOT / 'src/http_assets.cpp'), str(ROOT / 'tests/local_http_bridge.cpp'), '-o', str(path)], check=True)
    native = ctypes.CDLL(str(path)); u32 = ctypes.POINTER(ctypes.c_uint32)
    native.local_http_initialize.argtypes = [u32, ctypes.c_char_p]
    native.local_http_operation.argtypes = [ctypes.c_uint, ctypes.c_char_p]
    native.local_http_operation.restype = ctypes.c_int
    native.local_http_values.argtypes = [u32]; native.local_http_values.restype = ctypes.c_char_p
    native.local_http_trace.argtypes = [u32, ctypes.c_size_t]; native.local_http_trace.restype = ctypes.c_size_t
    native.local_http_body.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]; native.local_http_body.restype = ctypes.c_size_t
    return native, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def native_case(native, item):
    native.local_http_initialize((ctypes.c_uint32 * 6)(*item['initial']), item['path'].encode())
    snapshots = []
    for line in item['lines']:
        result = native.local_http_operation(OPERATIONS.index(item['op']), None if line is None else line.encode())
        if result: raise AssertionError('Native HTTP operation failed: %s (%d)' % (item['id'], result))
        values = (ctypes.c_uint32 * 8)()
        path = native.local_http_values(values).decode()
        snapshots.append(dict(values=list(values), path=path))
    trace = (ctypes.c_uint32 * 128)(); size = native.local_http_trace(trace, len(trace))
    if size > len(trace): raise AssertionError('Trace overflow')
    body = (ctypes.c_uint8 * 32768)(); length = native.local_http_body(body, len(body))
    if length > len(body): raise AssertionError('Body overflow')
    return dict(id=item['id'], snapshots=snapshots,
                events=[list(trace[i:i + 4]) for i in range(0, size, 4)],
                body_sha256=hashlib.sha256(bytes(body[:length])).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path)
    parser.add_argument('--original-jsonl', type=Path)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--export', type=Path)
    parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    cases = json.loads(args.input.read_text()) if args.input else input_cases()
    if args.write_evidence and args.input: parser.error('Evidence requires complete deterministic cases')
    serialized = (json.dumps(cases, separators=(',', ':')) + '\n').encode()
    if args.prepare_input:
        args.prepare_input.write_bytes(serialized)
        print('Prepared %u local HTTP cases' % len(cases))
    if not args.original_jsonl: return 0
    execution = json.loads(Path(str(args.original_jsonl) + '.json').read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if (digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA or
        execution['input_sha256'] != IMAGE_SHA or
        execution['input_file_sha256'] != hashlib.sha256(serialized).hexdigest()):
        raise SystemExit('Image or original execution input identity mismatch')
    if execution['cases'] != len(cases) or len(originals) != len(cases): raise SystemExit('Original case count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-local-http-') as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, originals):
            actual = native_case(native, item)
            if actual != expected:
                print(json.dumps(dict(input=item, original=expected, native=actual), indent=2))
                raise SystemExit('Local HTTP mismatch')
    paths = ['include/zseries/local_http.hpp', 'src/local_http.cpp', 'src/http_assets.cpp',
             'tests/local_http_bridge.cpp', 'tools/validate_local_http.py', 'tools/extract_http_assets.py',
             'tools/ghidra/EmulateLocalHttp.java', 'tools/ghidra/DisplayExecution.java']
    report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases), all_results_match=True,
        compiler=compiler, source_sha256={path: digest(ROOT / path) for path in paths},
        original_execution_jsonl_sha256=digest(args.original_jsonl), hardware_tested=False,
        **{key: execution[key] for key in ['scope', 'original_functions_executed', 'modeled_boundaries']})
    if args.export:
        report['original_instruction_sha256'] = {a: digest(args.export / 'instructions' / ('fn_' + a[2:] + '.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export: parser.error('--write-evidence requires --export')
        fixture = ROOT / 'tests/fixtures/local-http-original.json'
        fixture.write_text(json.dumps(dict(input_sha256=IMAGE_SHA,
            cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]), indent=2) + '\n')
        report['golden_fixture_sha256'] = digest(fixture)
        (ROOT / 'analysis/local-http-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print('All %u original/native local HTTP cases match' % len(cases))
    return 0


if __name__ == '__main__': raise SystemExit(main())
