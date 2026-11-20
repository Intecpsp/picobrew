#!/usr/bin/env python3
"""Compare the outgoing HTTP client with preserved Z119 instruction execution."""
import argparse
import ctypes
import gzip
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
IMAGE_SHA='d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
OPERATIONS=['parse','reset','transition','error','cancel','configure','authorization','get','post','put','tick']
WORDS=['state','class','present','error','socket_error','now','clock','date','deadline','reply_timeout','header_timeout','stalled','received','status','content_length',
       'argument','acquire_result','acquired_class','connect_result','write_short','heap_before','heap_after','connected','headers_accept','read_error_at','read_limit',
       'frozen','truncated','send_date','headers_date','body_date','send_duration','reply_duration','body_duration','body_present','body_size']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def case(name,op='tick',uri='http://192.168.1.73/path',wire='',value='payload',content_type='application/test',buffer='',**changes):
    values=dict(state=0,present=1,error=0,socket_error=0,now=1000,clock=1003,date=100,deadline=1000,reply_timeout=5000,header_timeout=5000,stalled=0,
        received=7,status=200,content_length=-1,argument=1,acquire_result=1,acquired_class=2,connect_result=1,write_short=0,heap_before=100000,heap_after=50000,
        connected=1,headers_accept=1,read_error_at=-1,read_limit=4096,frozen=0,truncated=0,send_date=80,headers_date=90,body_date=95,send_duration=1,reply_duration=2,body_duration=3,body_present=1,body_size=-1)
    values['class']=2;values.update(changes);return dict(id=name,op=op,words=[values[k]&0xffffffff for k in WORDS],uri=uri,wire=wire,value=value,content_type=content_type,buffer=buffer)
def input_cases():
    rows=[]
    uris=['','http://','https://','HTTP://host/','ftp://host/','http://example.test','https://example.test','http://example.test/','http://example.test/a?x=1',
          'http://example.test:8080/path','https://example.test:65432','http://host:-1/','http://host:65536/','http://host:70000/','http://host:abc/',
          'http://0.0.0.0/','http://127.0.0.1/','https://255.255.255.255/','http://1.2.3.256/','http://1.2.3/','http://.../','http://1..3.4/']
    for n in (1,63,64,65,80):uris.append('http://'+'h'*n+'/x')
    for n in (1,255,256,257,300):uris.append('https://host/'+'x'*n)
    for i,uri in enumerate(uris):rows.append(case(f'uri-{i}','parse',uri=uri))
    for state,target in itertools.product(range(7),repeat=2):
        for present in (0,1):rows.append(case(f'transition-{state}-{target}-{present}','transition',state=state,argument=target,present=present))
    for state in range(7):
        rows.append(case(f'reset-{state}','reset',state=state,buffer='old line',error=3,stalled=1))
        rows.append(case(f'cancel-{state}','cancel',state=state))
        for size in (0,1,511,512,513):rows.append(case(f'authorization-{state}-{size}','authorization',state=state,value='a'*size))
    for old,error in itertools.product((0,3,15),range(18)):rows.append(case(f'error-{old}-{error}','error',error=old,argument=error))
    for state,kind,acquire,selected in itertools.product((0,1,2),(0,1,2,3),(0,1),(0,2,3)):
        rows.append(case(f'configure-{state}-{kind}-{acquire}-{selected}','configure',state=state,argument=kind,acquire_result=acquire,acquired_class=selected))
    for i,uri in enumerate(uris):rows.append(case(f'configure-uri-{i}','configure',uri=uri,argument=2))
    for op,kind,connection,short in itertools.product(('get','post','put'),(0,1,2,3,4),(0,1,2),(0,1,2,3)):
        rows.append(case(f'{op}-{kind}-{connection}-{short}',op,state=1,connect_result=connection,write_short=short,socket_error=113,**{'class':kind}))
    for op,state,present,valid in itertools.product(('get','post','put'),range(6),(0,1),(0,1)):
        rows.append(case(f'send-guard-{op}-{state}-{present}-{valid}',op,state=state,present=present,uri='http://host/p' if valid else '',error=6 if state==4 else 0,stalled=state==3))
    for op,uri,content in itertools.product(('get','post'),('http://host/','https://host/','http://1.2.3.4:81/'),('', 'text/plain')):
        rows.append(case(f'send-wire-{len(rows)}',op,uri=uri,state=1,content_type=content,**{'class':3 if uri.startswith('https') else 2}))
    for op in ('post','put'):
        for state in (0,1,4):rows.append(case(f'null-payload-{op}-{state}',op,state=state,body_present=0,body_size=3))
    for before,after in itertools.product((0,60000,60001,100000),(0,15000,15001,19999,20000)):
        rows.append(case(f'heap-{before}-{after}','get',state=1,heap_before=before,heap_after=after))
    for state,now,connected in itertools.product(range(7),(0,999,1000,1001,0xffffffff),(0,1)):
        rows.append(case(f'empty-tick-{state}-{now}-{connected}',state=state,now=now,connected=connected))
    headers=['\r\n','\n','HTTP/1.1 200 OK\r\nContent-Length: 4\r\nContent-Type: text/plain\r\n\r\nDATA',
             'HTTP/1.0 200 OK\nContent-Length: 0\n\n','HTTP/1.1 404 Not Found\r\n\r\n','HTTP/1.1 500 Error\r\n',
             'HTTP/1.1 200 OK\r\ncontent-length: 4\r\n\r\n','HTTP/1.1 200 OK\r\nContent-Length: -1\r\nContent-Type: text/plain  \t\r\n\r\n',
             'HTTP/1.1 200 OK\r\nUnknown: value\r\n','HTTP/1.1 2\r\n','Content-Length: 12junk\r\n\r\n','Content-Type: '+'x'*65+'\r\n\r\n','X'*1023+'\n','X'*1024+'\n']
    for i,wire in enumerate(headers):
        # Preserve the original reader's pending line across a poll: the two
        # capacity cases start with the prefix already read on prior ticks.
        pending=wire[:-1] if i>=12 else '';chunk=wire[-1:] if i>=12 else wire
        for connected,accept in itertools.product((0,1),repeat=2):rows.append(case(f'headers-{i}-{connected}-{accept}',state=3,wire=chunk,buffer=pending,connected=connected,headers_accept=accept))
    for index in (0,1,15,30):rows.append(case(f'header-read-failure-{index}',state=3,wire=headers[2],read_error_at=index,socket_error=104))
    for frozen,truncated in itertools.product((0,1),repeat=2):rows.append(case(f'header-buffer-flags-{frozen}-{truncated}',state=3,wire='\n',buffer='old',frozen=frozen,truncated=truncated))
    for part in ('HTTP/1.1 ','Content-Len','partial\t'):
        rows.append(case(f'header-fragment-{len(rows)}',state=3,wire='200 OK\r\n\r\n',buffer=part))
    for length,declared,connected in itertools.product((0,1,8,1023,1024,1025,1500),(-1,0,1,8,2048),(0,1)):
        rows.append(case(f'body-{length}-{declared}-{connected}',state=4,wire='x'*length,content_length=declared,connected=connected))
    for state in (2,3,4):rows.append(case(f'negative-available-{state}',state=state,read_limit=-1,connected=1))
    rng=random.Random(0x40101bc4)
    for i in range(96):rows.append(case(f'random-{i}',state=rng.randrange(6),now=rng.randrange(2**32),deadline=rng.randrange(2**32),wire=chr(65+rng.randrange(26))*rng.randrange(64),
        connected=rng.randrange(2),content_length=rng.randrange(-1,70),headers_accept=rng.randrange(2),buffer='fragment' if rng.randrange(2) else ''))
    return rows
def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));path=Path(directory)/'http_client.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'src/http_client.cpp'),str(ROOT/'src/enums.cpp'),str(ROOT/'tests/http_client_bridge.cpp'),'-o',str(path)],check=True)
    native=ctypes.CDLL(str(path));u32=ctypes.POINTER(ctypes.c_uint32);native.http_client_test.argtypes=[ctypes.c_uint,u32]+[ctypes.c_char_p]*5+[u32,ctypes.c_size_t];native.http_client_test.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]
def native_case(native,item):
    words=(ctypes.c_uint32*len(WORDS))(*item['words']);output=(ctypes.c_uint32*40000)();strings=[item[k].encode('latin1') for k in ['uri','wire','value','content_type','buffer']]
    status=native.http_client_test(OPERATIONS.index(item['op']),words,*strings,output,len(output))
    if status:raise AssertionError(f'Native fixture failed {item["id"]}: {status}')
    length=output[2];offset=4+length;events=[]
    for _ in range(output[3]):n=output[offset];events.append(list(output[offset+1:offset+1+n]));offset+=n+1
    if offset!=output[0]+1:raise AssertionError('Bad fixture shape')
    return dict(id=item['id'],result=output[1],state=list(output[4:4+length]),events=events)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path);parser.add_argument('--input',type=Path);parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    cases=json.loads(args.input.read_text()) if args.input else input_cases()
    if args.input and args.write_evidence:parser.error('Evidence requires complete deterministic cases')
    serialized=json.dumps(cases,separators=(',',':'))+'\n'
    if args.prepare_input:args.prepare_input.write_text(serialized);print(f'Prepared {len(cases)} outgoing HTTP cases')
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()];input_hash=digest(args.input) if args.input else hashlib.sha256(serialized.encode()).hexdigest()
    if execution['input_sha256']!=IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Original image mismatch')
    if execution['input_file_sha256']!=input_hash or execution['cases']!=len(cases) or len(originals)!=len(cases):raise SystemExit('Original input/count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-http-client-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:print(json.dumps(dict(case=item,original=expected,native=actual),indent=2));raise SystemExit('Outgoing HTTP mismatch')
    paths=['include/zseries/http_client.hpp','src/http_client.cpp','include/zseries/still_http.hpp','src/still_http.cpp','platform/esp32/http_client_platform.hpp','platform/esp32/http_client_platform.cpp',
        'tests/http_client_bridge.cpp','tests/test_http_client.py','tools/validate_http_client.py','tools/ghidra/EmulateHttpClient.java','tools/ghidra/Z119Execution.java']
    report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,compiler=compiler,hardware_tested=False,original_execution_jsonl_sha256=digest(args.original_jsonl),input_cases_sha256=input_hash,
        source_sha256={p:digest(ROOT/p) for p in paths},**{key:execution[key] for key in ['scope','original_functions_executed','modeled_boundaries']})
    if args.export:report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export:parser.error('Evidence requires --export')
        goldens=[dict(input=i,original=o) for i,o in zip(cases,originals) if not i['id'].startswith('random-')];fixture=ROOT/'tests/fixtures/http-client-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA,cases=goldens),separators=(',',':'))+'\n').encode(),mtime=0));report['golden_cases']=len(goldens);report['golden_fixture_sha256']=digest(fixture);(ROOT/'analysis/http-client-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'All {len(cases)} original/native outgoing HTTP cases match')
if __name__=='__main__':main()
