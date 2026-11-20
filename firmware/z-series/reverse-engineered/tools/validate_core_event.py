#!/usr/bin/env python3
"""Compare CoreEvent initialization/reset and one-slot publication to Z119."""
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
from validate_display import IMAGE_SHA,digest
ROOT=Path(__file__).resolve().parents[1]
SIZE=0xdf0


def seed(value):
    data=bytearray(SIZE)
    def word(at,v):struct.pack_into('<I',data,at,v&0xffffffff)
    word(4,8);word(8,123+value);word(12,789+value)
    for i in range(20):
        at=16+176*i;word(at,i+100*value)
        text=('Recipe %d/%d'%(value,i)).encode();data[at+4:at+4+len(text)]=text
        word(at+72,len(text));word(at+76,64-len(text));data[at+80]=i%2;data[at+81]=(i//2)%2;word(at+84,i%4)
        p=at+88;word(p,24);word(p+4,-1+i);word(p+8,10+i)
        for q in range(4):struct.pack_into('<dI',data,p+16+16*q,12.25+value+i+q,q+1)
        data[p+80]=i%2
    for at,v in [(0xdd0,20),(0xdd4,20*value),(0xdd8,100*value),(0xde0,8),(0xde4,1),(0xde8,2),(0xdec,9)]:word(at,v)
    data[0xddc]=1
    return data.hex()


def input_cases():
    cases=[]
    def case(name,actions,pending=False,initial=1):
        cases.append(dict(id=name,current_hex=seed(initial),queued_hex=seed(3),pending=pending,actions=actions))
    case('constructor',[[0,0]])
    for value in list(range(15))+[0xffffffff]:case('reset-%u'%value,[[1,value]])
    for pending in [False,True]:
        case('publish-%u'%pending,[[2,0]],pending)
        case('notification-%u'%pending,[[3,0]],pending)
        case('constructor-publish-%u'%pending,[[0,0],[2,0]],pending)
        case('drop-retains-first-%u'%pending,[[2,0],[1,7],[2,0]],pending)
        case('ready-twice-%u'%pending,[[3,0],[1,8],[3,0]],pending)
    return cases


def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));library=Path(directory)/'core_event.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC',
        '-I',str(ROOT/'include'),str(ROOT/'src/program_list.cpp'),str(ROOT/'tests/core_event_bridge.cpp'),'-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));p=ctypes.POINTER(ctypes.c_uint8)
    native.core_event_initialize.argtypes=[p,p,ctypes.c_bool];native.core_event_action.argtypes=[ctypes.c_uint,ctypes.c_uint32]
    native.core_event_snapshot.argtypes=[p,p];native.core_event_snapshot.restype=ctypes.c_bool
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]


def native_case(native,item):
    current=(ctypes.c_uint8*SIZE).from_buffer_copy(bytes.fromhex(item['current_hex']))
    queued=(ctypes.c_uint8*SIZE).from_buffer_copy(bytes.fromhex(item['queued_hex']))
    native.core_event_initialize(current,queued,item['pending']);snapshots=[]
    for action,arg in item['actions']:
        native.core_event_action(action,arg);pending=native.core_event_snapshot(current,queued)
        snapshots.append(dict(pending=pending,current_sha256=hashlib.sha256(bytes(current)[4:]).hexdigest(),
            queued_sha256=hashlib.sha256(bytes(queued)[4:]).hexdigest()))
    return dict(id=item['id'],snapshots=snapshots)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path)
    parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true')
    args=parser.parse_args();cases=input_cases();serialized=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if args.prepare_input:args.prepare_input.write_bytes(serialized);print('Prepared %u CoreEvent cases'%len(cases))
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(s) for s in args.original_jsonl.read_text().splitlines()]
    if digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Unexpected original image')
    if execution['input_sha256']!=IMAGE_SHA or execution['input_file_sha256']!=hashlib.sha256(serialized).hexdigest():raise SystemExit('Original input identity mismatch')
    if len(originals)!=len(cases) or execution['cases']!=len(cases):raise SystemExit('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='z-core-event-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps(dict(id=item['id'],original=expected,native=actual),indent=2));raise SystemExit('CoreEvent mismatch')
    if args.write_evidence:
        if not args.export:parser.error('Instruction export required')
        fixture=ROOT/'tests/fixtures/core-event-original.json'
        fixture.write_text(json.dumps(dict(input_sha256=IMAGE_SHA,cases=[dict(input=i,original=o) for i,o in zip(cases,originals)]),indent=2)+'\n')
        paths=['include/zseries/program_list.hpp','src/program_list.cpp','tests/core_event_fixture.hpp','tests/core_event_bridge.cpp',
            'tools/validate_core_event.py','tools/ghidra/EmulateCoreEvent.java','tools/ghidra/DisplayExecution.java']
        report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,hardware_tested=False,compiler=compiler,
            source_sha256={p:digest(ROOT/p) for p in paths},golden_fixture_sha256=digest(fixture),
            original_execution_jsonl_sha256=digest(args.original_jsonl),
            **{k:execution[k] for k in ['scope','original_functions_executed','modeled_boundaries']})
        report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
        (ROOT/'analysis/core-event-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All %u original/native CoreEvent cases match'%len(cases))


if __name__=='__main__':main()
