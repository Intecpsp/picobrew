#!/usr/bin/env python3
"""Compare the complete /state document with original Z119 JSON instructions."""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
from validate_display import IMAGE_SHA,digest
ROOT=Path(__file__).resolve().parents[1]


def bits(value):return struct.pack('>d',value).hex()


def input_cases():
    cases=[]
    def case(name,numbers=None,values=None,step='Mash'):
        cases.append(dict(id=name,numbers=[bits(v) for v in (numbers or [90.125,65.25,21.75,39.5,66.0])],
            values=values or [6,0x30,0,0xffffffba,123,456,1],step=step))
    for valve in [0,1,2,3,4,5,6,7,8,9,10,255]:
        for pak in [0,1]:case('location-%u-%u'%(valve,pak),values=[valve,0xf0,0,0xffffffba,123,456,pak])
    for pause in range(18):case('pause-%u'%pause,values=[2,0x10,pause,0x80000000,0xffffffff,0xffffffff,0xfffffffe])
    for pumps in [0,1,0x10,0x20,0x30,0x40,0xff]:case('pumps-%u'%pumps,values=[4,pumps,0,2147483647,0,0,0])
    values=[0.0,-0.0,1.0,-1.0,1.234567890123,0.1234567895,9999999.9999,9.9999999995,
        math.nextafter(1e-5,0),1e-5,math.nextafter(1e-5,1),math.nextafter(1e7,0),1e7,math.nextafter(1e7,math.inf),
        1e-9,1e-100,1e-300,5e-324,1e20,1e100,1e308,math.inf,-math.inf,math.nan]
    for i,value in enumerate(values):case('double-%u'%i,numbers=[value]*5)
    for i,name in enumerate([None,'','x'*64,'quote" backslash\\ tab\t cr\r lf\n back\b form\f',
                              'raw\x01control\x1f','Café / 麦芽','x'*2500]):case('string-%u'%i,step=name)
    return cases


def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));library=Path(directory)/'http_state.so'
    paths=['src/local_http.cpp','src/http_assets.cpp','src/local_http_server.cpp','src/local_http_state.cpp','tests/local_http_state_bridge.cpp']
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off',
        '-shared','-fPIC','-I',str(ROOT/'include')]+[str(ROOT/p) for p in paths]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));native.http_state_run.argtypes=[ctypes.POINTER(ctypes.c_uint64),ctypes.POINTER(ctypes.c_uint32),ctypes.c_char_p]
    native.http_state_response.restype=ctypes.POINTER(ctypes.c_uint32)
    native.http_state_output.argtypes=[ctypes.POINTER(ctypes.c_uint8),ctypes.c_size_t];native.http_state_output.restype=ctypes.c_size_t
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]


def native_case(native,item):
    result=native.http_state_run((ctypes.c_uint64*5)(*[int(v,16) for v in item['numbers']]),
        (ctypes.c_uint32*7)(*item['values']),None if item['step'] is None else item['step'].encode())
    if result:raise AssertionError('State response failed '+item['id'])
    output=(ctypes.c_uint8*2048)();size=native.http_state_output(output,len(output))
    if size>len(output):raise AssertionError('Output overflow')
    return dict(id=item['id'],response=list(native.http_state_response()[:4]),output_hex=bytes(output[:size]).hex())


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path)
    parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true')
    args=parser.parse_args();cases=input_cases();serialized=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if args.prepare_input:args.prepare_input.write_bytes(serialized);print('Prepared %u /state cases'%len(cases))
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if execution['input_sha256']!=IMAGE_SHA or execution['input_file_sha256']!=hashlib.sha256(serialized).hexdigest():raise SystemExit('Original execution identity mismatch')
    if digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Unexpected original image')
    if execution['cases']!=len(cases) or len(originals)!=len(cases):raise SystemExit('Incomplete original output')
    with tempfile.TemporaryDirectory(prefix='z-http-state-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps(dict(input=item,original=expected,native=actual),indent=2));raise SystemExit('State mismatch')
    if args.write_evidence:
        if not args.export:parser.error('Evidence requires original instruction export')
        fixture=ROOT/'tests/fixtures/local-http-state-original.json'
        fixture.write_text(json.dumps(dict(input_sha256=IMAGE_SHA,cases=[dict(input=i,original=o) for i,o in zip(cases,originals)]),indent=2)+'\n')
        paths=['include/zseries/local_http_state.hpp','src/local_http_state.cpp','tests/local_http_state_bridge.cpp',
            'tools/validate_local_http_state.py','tools/ghidra/EmulateLocalHttpState.java','tools/ghidra/DisplayExecution.java']
        report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,compiler=compiler,hardware_tested=False,
            source_sha256={p:digest(ROOT/p) for p in paths},golden_fixture_sha256=digest(fixture),
            original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{key:execution[key] for key in ['scope','original_functions_executed','modeled_boundaries']})
        report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
        (ROOT/'analysis/local-http-state-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All %u original/native /state cases match'%len(cases))


if __name__=='__main__':main()
