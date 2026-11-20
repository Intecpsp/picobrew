#!/usr/bin/env python3
"""Compare complete outgoing HTTP assertions, timing, diagnostics and I/O."""
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
import validate_http_client as legacy

ROOT=Path(__file__).resolve().parents[1]
IMAGE_SHA=legacy.IMAGE_SHA
OPERATIONS=legacy.OPERATIONS+["request_out","timing_out"]
SOURCES=['include/zseries/http_client.hpp','src/http_client.cpp','include/zseries/console.hpp',
    'include/zseries/enums.hpp','src/enums.cpp','include/zseries/diagnostics.hpp','src/diagnostics.cpp',
    'tests/http_client_continuation_bridge.cpp','tests/test_http_client_continuation.py',
    'tests/http_client_continuation_contracts.cpp','tools/validate_http_client.py',
    'tools/validate_http_client_continuation.py','tools/ghidra/EmulateHttpClientContinuation.java',
    'tools/ghidra/Z119Execution.java','tests/run_http_client_continuation_emulation.py']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def input_cases():
    rows=[]
    for i,item in enumerate(legacy.input_cases()):
        item.update(assertion_count=0xffffffff if i%3==0 else 0,trace=False);rows.append(item)
    for clock in (0,16777217,0xfffffff0):
        for state,target in ((0,3),(1,3),(2,4),(4,5)):
            item=legacy.case(f'timing-{clock}-{state}-{target}','transition',state=state,argument=target,clock=clock,send_date=0,headers_date=0xffffffff)
            item.update(assertion_count=0xffffffff,trace=True);rows.append(item)
        for state in range(6):
            item=legacy.case(f'trace-send-{clock}-{state}','post',state=state,clock=clock,stalled=1,heap_before=0,heap_after=0)
            item.update(assertion_count=0xffffffff,trace=True);rows.append(item)
    for state,wire in [(3,'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n'),(4,'ABC'),(5,'')]:
        item=legacy.case(f'trace-response-{state}',state=state,wire=wire,connected=0,content_length=5)
        item.update(assertion_count=0,trace=True);rows.append(item)
    for clock in (0,16777217,0xfffffff0):
        item=legacy.case(f'timing-out-{clock}','timing_out',clock=clock,send_duration=0xffffffff,reply_duration=0x80000000,body_duration=17)
        item.update(assertion_count=0,trace=True);rows.append(item)
    for index,uri in enumerate(('','https://host:8443/path','http://192.168.1.2/')):
        item=legacy.case(f'request-out-{index}','request_out',uri=uri)
        item.update(assertion_count=0,trace=True);rows.append(item)
    return rows

def serialized_inputs():return (json.dumps(input_cases(),separators=(',',':'))+'\n').encode()
def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));output=Path(directory)/'http_continuation.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I'+str(ROOT/'include'),
        *[str(ROOT/p)for p in ['src/http_client.cpp','src/enums.cpp','src/diagnostics.cpp','tests/http_client_continuation_bridge.cpp']],'-o',str(output)],check=True)
    native=ctypes.CDLL(str(output));u32=ctypes.POINTER(ctypes.c_uint32)
    native.http_client_continuation_test.argtypes=[ctypes.c_uint,ctypes.c_uint32,ctypes.c_uint,u32]+[ctypes.c_char_p]*5+[u32,ctypes.c_size_t]
    native.http_client_continuation_test.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]
def native_case(native,item):
    words=(ctypes.c_uint32*len(legacy.WORDS))(*item['words']);output=(ctypes.c_uint32*60000)()
    status=native.http_client_continuation_test(OPERATIONS.index(item['op']),item['assertion_count'],item['trace'],words,
        *[item[k].encode('latin1')for k in ['uri','wire','value','content_type','buffer']],output,len(output))
    if status:raise AssertionError(f'Native fixture failed {item["id"]}: {status}')
    length=output[2];offset=4+length;events=[]
    for _ in range(output[3]):
        n=output[offset];events.append(list(output[offset+1:offset+1+n]));offset+=n+1
    if offset!=output[0]+1:raise AssertionError('Bad fixture shape')
    return dict(id=item['id'],result=output[1],state=list(output[4:4+length]),events=events)
def mismatch(actual,expected):
    changes={k:dict(native=actual[k],original=expected[k])for k in ['id','result']if actual[k]!=expected[k]}
    if actual['state']!=expected['state']:
        changes['state']=[dict(index=i,native=a,original=b)for i,(a,b)in enumerate(zip(actual['state'],expected['state']))if a!=b][:12]
        changes['state_lengths']=[len(actual['state']),len(expected['state'])]
    if actual['events']!=expected['events']:
        changes['event_counts']=[len(actual['events']),len(expected['events'])]
        for i in range(max(len(actual['events']),len(expected['events']))):
            a=actual['events'][i]if i<len(actual['events'])else None;b=expected['events'][i]if i<len(expected['events'])else None
            if a!=b:changes['first_event']=dict(index=i,native=a,original=b);break
    return changes

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('prepare-input','original-jsonl','export'):parser.add_argument('--'+name,type=Path)
    parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True,exist_ok=True);args.prepare_input.write_bytes(serialized_inputs());print('Prepared',len(input_cases()),'full outgoing HTTP cases')
    if not args.original_jsonl:return
    cases=input_cases();originals=[json.loads(line)for line in args.original_jsonl.read_text().splitlines()]
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text())
    if digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA or execution['input_sha256']!=IMAGE_SHA:raise ValueError('Original image changed')
    if execution['input_file_sha256']!=hashlib.sha256(serialized_inputs()).hexdigest() or execution['cases']!=len(cases) or len(originals)!=len(cases):raise ValueError('Incomplete/different original execution')
    if not execution['original_functions_executed'].get('0x400db014'):raise ValueError('Original assertion body did not execute')
    with tempfile.TemporaryDirectory(prefix='z-http-continuation-')as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps(dict(case=item['id'],difference=mismatch(actual,expected)),indent=2));raise SystemExit('Original/native outgoing HTTP continuation mismatch')
    if args.write_evidence:
        if not args.export:parser.error('--export required with --write-evidence')
        fixture=ROOT/'tests/fixtures/http-client-continuation-original.json.gz'
        fixture.write_bytes(gzip.compress((json.dumps(dict(input_sha256=IMAGE_SHA,inputs=cases,original=originals,execution=execution),separators=(',',':'))+'\n').encode(),mtime=0))
        report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,hardware_tested=False,compiler=compiler,
            source_sha256={p:digest(ROOT/p)for p in SOURCES},fixture_sha256=digest(fixture),original_execution_jsonl_sha256=digest(args.original_jsonl),
            assertion_calls=execution['original_functions_executed']['0x400db014'],diagnostic_logs=sum(row[0]==18 for case in originals for row in case['events']),
            **{k:execution[k]for k in ['scope','original_functions_executed','modeled_boundaries']})
        report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json'))for a in report['original_functions_executed']}
        (ROOT/'analysis/http-client-continuation-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All',len(cases),'original/native outgoing HTTP continuation cases match')
if __name__=='__main__':main()
