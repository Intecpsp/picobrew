#!/usr/bin/env python3
"""Compare incoming HTTP lifecycle/framing/output with original Z119 instructions."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]


def input_cases():
    cases = []
    def case(name, actions, enabled=1, connected=1, deadline=2000, path='', buffer='', values=None):
        state = values or [enabled, deadline, 0, len(buffer), 2048-len(buffer), 0, 0,
                           0, 0, 0, 0, 0, len(path), 64-len(path), 0, connected]
        cases.append(dict(id=name, initial=state, path=path, buffer=buffer, actions=actions))
    def tick(data='', now=1000, accept=False, connection=-1):
        return dict(op='tick', argument=now, data=list(data.encode()) if isinstance(data, str) else data,
                    accept=accept, connection=connection)
    def enable(value): return dict(op='enable', argument=int(value), data=[], accept=False, connection=-1)
    for current in [0, 1]:
        for value in [0, 1]:
            case('enable-%d-%d' % (current, value), [enable(value), enable(value), enable(not value)], enabled=current)
    case('disabled', [tick('GET /ping HTTP/1.0\r\n\r\n')], enabled=0)
    case('no-accepted-client', [tick(), tick()], connected=0)
    case('accept-defers-reading', [tick('ignored input', accept=True), tick('GET /ping HTTP/1.0\r\n\r\n')], connected=0)
    case('accept-resets-previous', [tick(accept=True)], connected=0,
         path='/old', buffer='prior', values=[1, 6, 1, 5, 2043, 1, 1, 5, 99, 1, 1, 1, 4, 60, 1, 0])
    for now, deadline in [(0,0),(1,0),(1999,2000),(2000,2000),(2001,2000),
                          (0xffffffff,0xffffffff),(0,0xffffffff),(0xffffffff,0)]:
        case('deadline-%u-%u' % (now,deadline), [tick('GET /ping HTTP/1.0\r\n\r\n',now)], deadline=deadline)
    case('accept-deadline-wrap', [tick(now=0xfffffff0,accept=True), tick(now=0xfffffff1)], connected=0)
    for method in ['GET','POST','GETTING','POSTSCRIPT','get','HEAD','PUT']:
        for path in ['/ping','/state','/recipe','/zstate.css','/missing']:
            case('request-%s-%s' % (method,path), [tick(method+' '+path+' HTTP/1.0\r\n\r\n')])
    for ending in ['\n','\r\n',' \t\r\n','\v\n','\f\n']:
        case('line-ending-'+ending.encode().hex(), [tick('GET /ping HTTP/1.0'+ending+ending)])
    case('blank-line-with-spaces', [tick('GET /ping HTTP/1.0\n \t\r\n')])
    case('fragmented-lines-and-body', [tick('POS'), tick('T /recipe HTTP/1.0\r'),
         tick('\nContent-Len'), tick('gth: 7\r\nContent-Type: application/json\r\n\r\nabc'), tick('defgTAIL')])
    case('fragmented-nul-body', [tick('POST /recipe HTTP/1.0\nContent-Length: 3\n\nab\x00'), tick('\x00c')])
    case('header-nuls-ignored', [tick('G\x00ET /pi\x00ng HTTP/1.0\r\n\r\n')])
    case('completed-keeps-trailing-pipeline-unread', [tick('GET /ping HTTP/1.0\n\nGET /state HTTP/1.0\n\n')])
    for length in [0,1,2,63,64,65,2047,2048,2147483647,-1]:
        for send in [False,True]:
            body='x'*min(max(length,0),2050) if send else ''
            case('body-%d-%d' % (length,send), [tick('POST /recipe HTTP/1.0\nContent-Length: %d\n\n' % length + body)])
    for length in [2047,2048,2049,2200]:
        case('long-header-%d' % length, [tick('GET /ping HTTP/1.0\nX: '+'x'*length+'\n\n')])
        case('long-request-%d' % length, [tick('GET /'+'x'*length+' HTTP/1.0\n\n')])
    for read in [256,0xffffffff]:
        case('invalid-read-%u' % read, [tick(list(b'GET /ping')+[read]+list(b'\n\n'))])
    case('disconnect-before-next-fragment', [tick('GET /'),tick(accept=False,connection=0)])
    case('completed-entry', [tick('unused')], path='/ping',
         values=[1,2000,1,0,2048,0,0,1,0,0,1,1,5,59,0,1])
    for status in [200,400,404,500,0xffffffff]:
        for length in [-1,0,5,2047,2147483647]:
            for mime in [None,'','application/json']:
                case('headers-%u-%d-%s' % (status,length,mime),
                     [dict(op='headers',status=status,length=length,mime=mime)])
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    library = Path(directory) / 'http_server.so'
    sources = ['src/local_http.cpp','src/http_assets.cpp','src/local_http_server.cpp','tests/local_http_server_bridge.cpp']
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-shared',
        '-I',str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library)); u32=ctypes.POINTER(ctypes.c_uint32)
    native.http_server_initialize.argtypes=[u32,ctypes.c_char_p,ctypes.c_char_p]
    native.http_server_step.argtypes=[ctypes.c_uint,ctypes.c_uint32,u32,ctypes.c_size_t,ctypes.c_bool,ctypes.c_int]
    native.http_server_headers.argtypes=[ctypes.c_uint32,ctypes.c_int32,ctypes.c_char_p]
    native.http_server_values.argtypes=[u32]
    for name in ['http_server_buffer','http_server_path']:
        getattr(native,name).restype=ctypes.c_char_p
    native.http_server_bytes.argtypes=[ctypes.c_uint,ctypes.POINTER(ctypes.c_uint8),ctypes.c_size_t]
    native.http_server_bytes.restype=ctypes.c_size_t
    native.http_server_trace.argtypes=[u32,ctypes.c_size_t];native.http_server_trace.restype=ctypes.c_size_t
    return native, subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]


def native_case(native,item):
    native.http_server_initialize((ctypes.c_uint32*16)(*item['initial']),item['path'].encode(),item['buffer'].encode())
    snapshots=[]
    for action in item['actions']:
        if action['op']=='headers':
            result=native.http_server_headers(action['status'],action['length'],None if action['mime'] is None else action['mime'].encode())
        else:
            values=(ctypes.c_uint32*len(action['data']))(*action['data'])
            result=native.http_server_step(action['op']=='enable',action['argument'],values,len(values),action['accept'],action['connection'])
        if result: raise AssertionError('HTTP server failure %s %d' % (item['id'],result))
        state=(ctypes.c_uint32*16)();native.http_server_values(state)
        snapshots.append(dict(values=list(state),buffer_hex=native.http_server_buffer().hex(),path_hex=native.http_server_path().hex()))
    trace=(ctypes.c_uint32*100000)();size=native.http_server_trace(trace,len(trace))
    if size>len(trace): raise AssertionError('Trace overflow')
    output=dict(id=item['id'],snapshots=snapshots,event_count=size//4,
        events_sha256=hashlib.sha256(struct.pack('<%uI'%size,*trace[:size])).hexdigest())
    for kind,key in enumerate(['output_sha256','post_sha256']):
        body=(ctypes.c_uint8*65536)();size=native.http_server_bytes(kind,body,len(body))
        if size>len(body): raise AssertionError('Body overflow')
        output[key]=hashlib.sha256(bytes(body[:size])).hexdigest()
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input',type=Path);parser.add_argument('--original-jsonl',type=Path)
    parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true')
    args=parser.parse_args();cases=input_cases();serialized=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if args.prepare_input: args.prepare_input.write_bytes(serialized);print('Prepared %u HTTP server cases'%len(cases))
    if not args.original_jsonl:return 0
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text())
    originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if execution['input_sha256']!=IMAGE_SHA or execution['input_file_sha256']!=hashlib.sha256(serialized).hexdigest():
        raise SystemExit('Original execution identity mismatch')
    if digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Unexpected original image')
    if len(originals)!=len(cases) or execution['cases']!=len(cases):raise SystemExit('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='z-http-server-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps(dict(input=item,original=expected,native=actual),indent=2));raise SystemExit('HTTP server mismatch')
    if args.write_evidence:
        if not args.export:parser.error('Evidence requires instruction export')
        fixture=ROOT/'tests/fixtures/local-http-server-original.json'
        fixture.write_text(json.dumps(dict(input_sha256=IMAGE_SHA,cases=[dict(input=i,original=o) for i,o in zip(cases,originals)]),indent=2)+'\n')
        paths=['include/zseries/local_http_server.hpp','src/local_http_server.cpp','tests/local_http_server_bridge.cpp',
               'tools/validate_local_http_server.py','tools/ghidra/EmulateLocalHttpServer.java','tools/ghidra/DisplayExecution.java']
        report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,compiler=compiler,
            source_sha256={p:digest(ROOT/p) for p in paths},golden_fixture_sha256=digest(fixture),hardware_tested=False,
            original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{key:execution[key] for key in ['scope','original_functions_executed','modeled_boundaries']})
        report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
        (ROOT/'analysis/local-http-server-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All %u original/native HTTP server cases match'%len(cases))


if __name__=='__main__':main()
