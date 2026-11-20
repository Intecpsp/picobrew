#!/usr/bin/env python3
"""Compare the PicoStill controller with independently executed Z119 instructions."""
import argparse
import ctypes
import gzip
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
OPERATIONS=['advance','transition','retry','wait','scan','setup','tick']
WORDS=['state','previous','now','poll_date','poll_delay','maximum','threshold','retry','pending','error','retryable','done',
       'auth_pending','authorized','cleaned','command_pending','command','selection','wifi_pending','wifi','allow','scan_scenario',
       'scan_count','ip','sample_date','sample_delta','response_date','failures','poll_stop','assertion','argument','next','auth_start']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def case(name,op='advance',**changes):
    values=dict(state=1,previous=19,now=1000,poll_date=900,poll_delay=2000,maximum=2,threshold=4,retry=1,pending=9,error=0,retryable=1,done=0,
                auth_pending=1,authorized=1,cleaned=1,command_pending=0,command=1,selection=2,wifi_pending=0,wifi=5,allow=1,scan_scenario=2,
                scan_count=12,ip=0,sample_date=100,sample_delta=200,response_date=300,failures=2,poll_stop=0,assertion=0,argument=1,next=10,auth_start=1)
    values.update(changes);return dict(id=name,op=op,words=[values[key]&0xffffffff for key in WORDS])
def input_cases():
    rows=[]
    for previous in range(21):
        for target in range(21):rows.append(case(f'transition-{previous}-{target}','transition',state=previous,argument=target))
    for state in range(22):
        rows.append(case(f'idle-tick-{state}','tick',state=state))
        rows.append(case(f'setup-{state}','setup',state=state))
        for command in range(10):rows.append(case(f'command-{state}-{command}',state=state,command_pending=1,command=command))
        for wifi in range(8):
            for pending in (0,1):rows.append(case(f'wifi-priority-{state}-{wifi}-{pending}',state=state,wifi_pending=1,wifi=wifi,command_pending=pending,command=4))
    for state in (9,10,11,12,14,15,16,17,18):
        for status in range(4):
            for retry in (-1,0,1,2,3,0x7fffffff):
                for pending in (0,9):rows.append(case(f'client-{state}-{status}-{retry}-{pending}',state=state,error=status&1,done=status>>1,retry=retry,pending=pending))
    for state in (5,7):
        for pending in (0,1):
            for authorized in (0,1):
                for cleaned in (0,1):rows.append(case(f'authorization-{state}-{pending}-{authorized}-{cleaned}',state=state,auth_pending=pending,authorized=authorized,cleaned=cleaned))
    for command in range(13):
        for retry in (-2,-1,0,1,2,3,0x7fffffff):
            for maximum in (-1,0,1,2,3,0x7fffffff):rows.append(case(f'retry-{command}-{retry}-{maximum}','retry',argument=command,retry=retry,maximum=maximum))
    for now in (0,899,900,901,0xfffffff0,0xffffffff):
        for stopped in (0,1):
            for command in (0,6,7,8):rows.append(case(f'poll-{now}-{stopped}-{command}',state=17,pending=0,now=now,poll_stop=stopped,command_pending=command!=0,command=command))
    for retryable in (0,1):
        for failures in (-1,0,3,4,5,0x7fffffff):rows.append(case(f'poll-errors-{retryable}-{failures}',state=17,error=1,retryable=retryable,failures=failures))
    for count in (-2,-1,0,1,2,7,8,12,260):
        for scenario in range(3):rows.append(case(f'scan-{count}-{scenario}','scan',scan_count=count,scan_scenario=scenario))
    for index in range(8):rows.append(case(f'selection-{index}',state=3,command_pending=1,command=2,selection=index))
    for ip in range(3):
        for state in (10,12,13):rows.append(case(f'ip-{state}-{ip}',state=state,ip=ip,done=1))
    rows.append(case('runtime-gate-denied',command_pending=1,allow=0))
    rng=random.Random(0x4011a818)
    for index in range(128):rows.append(case(f'random-{index}',state=rng.randrange(21),command_pending=rng.randrange(2),command=rng.randrange(10),
        wifi_pending=rng.randrange(2),wifi=rng.randrange(8),pending=rng.randrange(12),error=rng.randrange(2),done=rng.randrange(2),retryable=rng.randrange(2),
        retry=rng.randrange(5),poll_stop=rng.randrange(2),auth_pending=rng.randrange(2),authorized=rng.randrange(2),cleaned=rng.randrange(2),now=rng.randrange(2**32)))
    return rows
def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));path=Path(directory)/'pico_still.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'src/pico_still.cpp'),
        str(ROOT/'src/eeprom.cpp'),str(ROOT/'src/enums.cpp'),str(ROOT/'tests/pico_still_bridge.cpp'),'-o',str(path)],check=True)
    native=ctypes.CDLL(str(path));u32=ctypes.POINTER(ctypes.c_uint32);native.pico_still_test.argtypes=[ctypes.c_uint,u32,u32,ctypes.c_size_t];native.pico_still_test.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]
def native_case(native,item):
    words=(ctypes.c_uint32*len(WORDS))(*item['words']);output=(ctypes.c_uint32*24000)()
    status=native.pico_still_test(OPERATIONS.index(item['op']),words,output,len(output))
    if status:raise AssertionError(f'Native fixture failed {item["id"]}: {status}')
    length=output[2];offset=4+length;events=[]
    for _ in range(output[3]):
        n=output[offset];events.append(list(output[offset+1:offset+1+n]));offset+=n+1
    if offset!=output[0]+1:raise AssertionError('Bad fixture shape')
    return dict(id=item['id'],result=output[1],state=list(output[4:4+length]),events=events)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path);parser.add_argument('--input',type=Path)
    parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    cases=json.loads(args.input.read_text()) if args.input else input_cases()
    if args.input and args.write_evidence:parser.error('Evidence requires complete deterministic cases')
    serialized=json.dumps(cases,separators=(',',':'))+'\n'
    if args.prepare_input:args.prepare_input.write_text(serialized);print(f'Prepared {len(cases)} PicoStill controller cases')
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    input_hash=digest(args.input) if args.input else hashlib.sha256(serialized.encode()).hexdigest()
    if execution['input_sha256']!=IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Original image mismatch')
    if execution['input_file_sha256']!=input_hash or execution['cases']!=len(cases) or len(originals)!=len(cases):raise SystemExit('Original input/count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-pico-still-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps(dict(case=item,original=expected,native=actual),indent=2));raise SystemExit('PicoStill mismatch')
    paths=['include/zseries/pico_still.hpp','src/pico_still.cpp','tests/pico_still_bridge.cpp','tests/test_pico_still.py','tools/validate_pico_still.py',
           'tools/ghidra/EmulatePicoStill.java','tools/ghidra/Z119Execution.java']
    report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,compiler=compiler,hardware_tested=False,
        original_execution_jsonl_sha256=digest(args.original_jsonl),input_cases_sha256=input_hash,source_sha256={p:digest(ROOT/p) for p in paths},
        **{key:execution[key] for key in ['scope','original_functions_executed','modeled_boundaries']})
    if args.export:report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export:parser.error('Evidence requires --export')
        goldens=[dict(input=i,original=o) for i,o in zip(cases,originals) if not i['id'].startswith('random-')]
        fixture=ROOT/'tests/fixtures/pico-still-original.json.gz'
        payload='{"input_sha256":'+json.dumps(IMAGE_SHA)+',"cases":[\n'+',\n'.join(json.dumps(row,separators=(',',':')) for row in goldens)+'\n]}\n'
        fixture.write_bytes(gzip.compress(payload.encode(),mtime=0))
        report['golden_cases']=len(goldens);report['golden_fixture_sha256']=digest(fixture);(ROOT/'analysis/pico-still-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'All {len(cases)} original/native PicoStill controller cases match')
if __name__=='__main__':main()
