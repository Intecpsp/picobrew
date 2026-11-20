#!/usr/bin/env python3
"""Compare reconstructed Z display operations with original Xtensa execution."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
IMAGE_SHA='d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
OPERATIONS=['clear','command','flush','clear_flush','invert','setup','rectangle','bitmap','geometry']

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def pattern(count,seed,multiplier):return bytes(((i*multiplier+seed)^(i>>3))&255 for i in range(count))
def case(name,op,**values):
    return dict(id=name,op=op,seed=19,cs=0,dc=2,reset=4,value=0,**values)

def input_cases():
    cases=[]
    for op in ['clear','flush','clear_flush','setup']:
        for index,(cs,dc,reset) in enumerate([(0,2,4),(0,2,0),(9,7,4)]):
            item=case(f'{op}-{index}',op);item.update(cs=cs,dc=dc,reset=reset,seed=index*31);cases.append(item)
    for value in range(256):
        item=case(f'command-{value}','command');item['value']=value;cases.append(item)
    for value in [0,1]:
        item=case(f'invert-{value}','invert');item['value']=value;cases.append(item)
    rectangles=[[0,0,127,63],[0,0,0,0],[1,0,1,0],[126,63,127,63],[-4,-3,7,9],[120,57,136,80],
                [-10,5,-1,9],[1,70,20,80],[5,4,8,7],[4,4,9,8],[5,4,9,8],[4,4,8,8],[0,0,0,63],[1,0,1,63]]
    for index,r in enumerate(rectangles):
        for value in [0,1,7,15,0x1e,0xf0,255]:
            for filled in [0,1]:
                item=case(f'rectangle-{index}-{value}-{filled}','rectangle',rect=r,filled=filled);item['value']=value;cases.append(item)
    generator=random.Random(119)
    for index in range(120):
        left,top=generator.randrange(-20,140),generator.randrange(-10,70)
        item=case(f'random-rectangle-{index}','rectangle',rect=[left,top,left+generator.randrange(1,25),top+generator.randrange(1,16)],filled=index%2)
        item.update(value=generator.randrange(256),seed=index);cases.append(item)
    for index,(width,height,pad) in enumerate([(1,1,0),(2,2,0),(7,9,0),(16,16,0),(19,11,3),(136,68,0)]):
        regions=[[0,0,width-1,height-1]]
        if width>2 and height>2:regions += [[1,1,width-2,height-2],[2,0,width-1,height-2]]
        for n,region in enumerate(regions):
            for p,point in enumerate([(0,0),(1,3),(2,4),(126,60),(-3,-4),(125,60),(129,65),(-160,-70)]):
                for invert in [0,1]:
                    item=case(f'bitmap-{index}-{n}-{p}-{invert}','bitmap',shape=[width,height,(width+1)//2+pad],rect=region,point=point)
                    item.update(value=invert,seed=index*17+n);cases.append(item)
    for index in range(200):
        a=[generator.randrange(-150,150) for _ in range(4)];b=[generator.randrange(-150,150) for _ in range(4)]
        if index%3:a=[min(a[0],a[2]),min(a[1],a[3]),max(a[0],a[2]),max(a[1],a[3])]
        if index%4:b=[min(b[0],b[2]),min(b[1],b[3]),max(b[0],b[2]),max(b[1],b[3])]
        valid=max(a[0],b[0])<=min(a[2],b[2]) and max(a[1],b[1])<=min(a[3],b[3])
        cases.append(case(f'geometry-{index}','geometry',rect=a,other=b,delta=[generator.randrange(-200,200),generator.randrange(-200,200)],intersect_valid=valid))
    return cases

class Event(ctypes.Structure):_fields_=[('kind',ctypes.c_uint32),('a',ctypes.c_uint32),('b',ctypes.c_uint32)]

def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));library=Path(directory)/'display.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-I',str(ROOT/'include'),
        str(ROOT/'src/display.cpp'),str(ROOT/'tests/display_bridge.cpp'),'-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));native.display_test.argtypes=[ctypes.c_uint,ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(Event),ctypes.c_size_t]
    native.display_test.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]

def native_case(native,item):
    args=[0]*32;args[:5]=[item['cs'],item['dc'],item['reset'],item['value'],item.get('filled',0)]
    args[5:9]=item.get('rect',[0]*4);args[9:13]=item.get('other',[0]*4);args[13:15]=item.get('delta',[0,0])
    args[15:18]=item.get('shape',[0,0,0]);args[18:20]=item.get('point',[0,0]);args[20]=item['seed']
    pixels=(ctypes.c_uint8*4096).from_buffer_copy(pattern(4096,item['seed'],73))
    transfer=(ctypes.c_uint8*4096).from_buffer_copy(pattern(4096,item['seed']+47,73));transmitted=(ctypes.c_uint8*4096)()
    results=(ctypes.c_uint32*20)();events=(Event*256)()
    status=native.display_test(OPERATIONS.index(item['op']),(ctypes.c_uint32*32)(*(x&0xffffffff for x in args)),pixels,transfer,transmitted,results,events,256)
    if status or results[0]:raise AssertionError(f'Native display rejected {item["id"]}: {status}/{results[0]}')
    trace=[]
    for event in events[:results[1]]:
        if event.kind<2:trace.append(['mode' if event.kind==0 else 'gpio',event.a,event.b])
        elif event.kind==2:trace.append(['spi',event.a])
        elif event.kind==3:trace.append(['spi_buffer',event.a,hashlib.sha256(bytes(transmitted)[:event.a]).hexdigest()])
        else:trace.append(['info' if event.kind==4 else 'register_commands'])
    values=[]
    if item['op']=='geometry':values=list(results[2:8])+[list(results[9:13]) if results[8] else [],list(results[13:17])]
    return {'id':item['id'],'result':values,'pixels_sha256':hashlib.sha256(bytes(pixels)).hexdigest(),
            'transfer_sha256':hashlib.sha256(bytes(transfer)).hexdigest(),'events':trace}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path)
    parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--input',type=Path);parser.add_argument('--export',type=Path)
    parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    cases=json.loads(args.input.read_text()) if args.input else input_cases()
    if args.write_evidence and args.input:parser.error('Evidence requires full deterministic cases')
    if args.prepare_input:args.prepare_input.write_text(json.dumps(cases,separators=(',',':'))+'\n');print(f'Prepared {len(cases)} display cases')
    if not args.original_jsonl:return 0
    if digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Wrong OEM image')
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text())
    originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if execution['input_sha256']!=IMAGE_SHA or execution['cases']!=len(cases) or len(originals)!=len(cases):raise SystemExit('Original execution identity/count mismatch')
    input_hash=digest(args.input) if args.input else hashlib.sha256((json.dumps(cases,separators=(',',':'))+'\n').encode()).hexdigest()
    if execution.get('input_file_sha256')!=input_hash:raise SystemExit('Original execution input hash mismatch')
    with tempfile.TemporaryDirectory(prefix='z-display-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps({'case':item,'expected':expected,'actual':actual},indent=2));raise SystemExit('Display mismatch')
    report={'schema_version':1,'input_sha256':IMAGE_SHA,'cases':len(cases),'all_results_match':True,
        'scope':execution['scope'],'original_functions_executed':execution['original_functions_executed'],
        'modeled_boundaries':execution['modeled_boundaries'],'original_execution_jsonl_sha256':digest(args.original_jsonl),
        'input_cases_sha256':hashlib.sha256(json.dumps(cases,separators=(',',':')).encode()).hexdigest(),
        'compiler':compiler,'hardware_tested':False,'source_sha256':{path:digest(ROOT/path) for path in
        ['src/display.cpp','include/zseries/display.hpp','tests/display_bridge.cpp','tools/ghidra/DisplayExecution.java','tools/ghidra/EmulateDisplay.java','tools/validate_display.py']}}
    if args.export:
        report['original_instruction_sha256']={address:digest(args.export/'instructions'/('fn_'+address[2:]+'.json')) for address in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export:parser.error('--write-evidence requires --export')
        goldens=[{'input':item,'original':expected} for item,expected in zip(cases,originals) if not item['id'].startswith('random-') and
            (item['op']!='command' or item['value'] in (0,1,15,128,255)) and (item['op']!='geometry' or int(item['id'].split('-')[-1])<32)]
        fixture=ROOT/'tests/fixtures/display-original.json'
        fixture.write_text(json.dumps({'input_sha256':IMAGE_SHA,'cases':goldens},indent=2)+'\n')
        report['golden_cases']=len(goldens);report['golden_fixture_sha256']=digest(fixture)
        (ROOT/'analysis/display-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'All {len(cases)} original/native display cases match')
    return 0
if __name__=='__main__':raise SystemExit(main())
