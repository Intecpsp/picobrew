#!/usr/bin/env python3
"""Compare real returning ADC assertions and complete Core tick continuation."""
from __future__ import annotations
import argparse
import ctypes
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import run_msp_emulation as common
IMAGE_SHA='d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
FLAGS=['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-shared','-fno-fast-math','-ffp-contract=off']
SOURCES=['include/zseries/sensor_acquisition.hpp','src/sensor_acquisition.cpp','include/zseries/temperature.hpp','src/temperature.cpp',
         'include/zseries/core_flow.hpp','src/core_flow.cpp','include/zseries/diagnostics.hpp','src/diagnostics.cpp','include/zseries/console.hpp',
         'tests/adc_continuation_bridge.cpp','tests/test_adc_continuation.py','tools/validate_adc_continuation.py',
         'tools/ghidra/EmulateAdcContinuation.java','tools/ghidra/EmulateCoreFlow.java','tools/ghidra/Z119Execution.java']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def case(name,kind='tick',bytes_=None,returned=8,now=1000,tick=1000,count=0,enabled=1,variant=0):
    w=[52,enabled,returned,now,tick,7,0,0]+[123]*4+[(i+variant)%4 for i in range(4)]+[99]*4
    v=[20.,30.,40.,50.]
    for channel in range(4):
        samples=[float(channel*10+i) for i in range(10)]
        total=sum(samples[:count]);v+=samples+[total,total/count if count else 0.]
        w += [count if count<10 else 0,count,1 if count else 0,1 if count else 0]
    return dict(id=name,kind=kind,words=w,values=v,bytes=bytes_ or [8,0]*4)
def input_cases():
    cases=[]
    for returned in list(range(8))+[9,255]:cases.append(case(f'poll-short-{returned}','poll',returned=returned))
    for index in range(8):
        for value in [-1,-128,-256,-2147483648]:
            b=[8,0]*4;b[index]=value
            cases.append(case(f'poll-negative-{index}-{value}','poll',bytes_=b))
    vectors=[[8,0]*4,[0,0,0,14,0,15,15,254],[15,255]*4,[-1,0]*4,[8,-1]*4,
             [8,0,-1,0,8,-1,8,0],[-256,15,-2147483648,15,-1,-1,0,256]]
    for index,b in enumerate(vectors):cases.append(case(f'poll-values-{index}','poll',bytes_=b))
    clocks=[(0,0),(1,0),(2000,0),(2001,0),(0xfffffff0,0),(0,1),(2001,5000),(0xfffffff0,0xfffffff0)]
    for i,(now,tick) in enumerate(clocks):
        cases.append(case(f'tick-disabled-{i}',now=now,tick=tick,enabled=0))
        for j,b in enumerate(vectors):
            for count in [0,5,10]:cases.append(case(f'tick-{i}-{j}-{count}',now=now,tick=tick,bytes_=b,count=count,variant=(i+j)%4))
    for now,tick in [(0,0),(1000,1000),(0xfffffff0,0)]:
        for returned in list(range(8))+[9,255]:cases.append(case(f'tick-short-{now}-{returned}',returned=returned,now=now,tick=tick))
    assert len({c['id'] for c in cases})==len(cases)
    return cases
def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));output=Path(directory)/'adc-continuation.so'
    subprocess.run(compiler+FLAGS+['-I',str(ROOT/'include')]+[str(ROOT/p) for p in
        ['tests/adc_continuation_bridge.cpp','src/sensor_acquisition.cpp','src/temperature.cpp','src/diagnostics.cpp','src/core_flow.cpp','src/eeprom.cpp']]+['-o',str(output)],check=True)
    native=ctypes.CDLL(str(output))
    native.adc_continuation_action.argtypes=[ctypes.c_uint,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_double),ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_size_t)]
    native.adc_continuation_action.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]
def native_case(native,item):
    w=(ctypes.c_uint32*36)(*item['words']);v=(ctypes.c_double*52)(*item['values']);b=(ctypes.c_int*8)(*item['bytes'])
    events=(ctypes.c_int*1024)();size=ctypes.c_size_t()
    result=native.adc_continuation_action(item['kind']=='tick',w,v,b,events,ctypes.byref(size))
    if size.value>1024 or size.value%4:raise AssertionError('Event bound exceeded')
    return result,dict(id=item['id'],words=list(w),values=list(v),events=[list(events[i:i+4]) for i in range(0,size.value,4)])
def compare_case(native,item,expected):
    result,actual=native_case(native,item)
    # Original routines return void. Retain owned error reporting separately.
    status=actual['words'][6];actual['words'][6]=0
    if result or actual['id']!=expected['id'] or actual['words']!=expected['words'] or actual['events']!=expected['events']:
        raise AssertionError(f"ADC continuation state/event mismatch {item['id']}: {actual} != {expected}")
    enabled=item['kind']=='poll' or item['words'][1]
    want=0
    if enabled:
        if item['words'][2]!=8:want=3
        elif any(x<0 for x in item['bytes']):want=4
        if item['kind']=='tick' and item['words'][2]==8 and item['words'][4]==0 and any(15<=x<=4094 for x in expected['words'][8:12]):want=5
    if status!=want:raise AssertionError(f'Unexpected owned status {item["id"]}: {status} != {want}')
    exact=0;maximum=0.
    for a,b in zip(actual['values'],expected['values']):
        exact+=struct.pack('<d',a)==struct.pack('<d',b);maximum=max(maximum,abs(a-b))
        if not math.isclose(a,b,rel_tol=0,abs_tol=2e-12):raise AssertionError(f'ADC numerical mismatch {item["id"]}: {a} != {b}')
    return exact,len(actual['values']),maximum
def verify_original(report,cases):
    raw=(ROOT.parent/'oem/upstream/zseries_0_0_119.bin').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=report['input_sha256']:raise ValueError('OEM input differs')
    encoded=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if hashlib.sha256(encoded).hexdigest()!=report['input_file_sha256']:raise ValueError('Original cases differ')
    segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in common.images.parse_image(raw)['segments']]
    for fn in report['original_functions']:
        for span in fn['ranges']:
            if hashlib.sha256(common.read_mapped(raw,segments,span['address'],span['size'])).hexdigest()!=span['sha256']:raise ValueError('Executed function bytes differ')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare-input',type=Path);p.add_argument('--original-jsonl',type=Path);p.add_argument('--write-evidence',action='store_true');a=p.parse_args();cases=input_cases()
    if a.prepare_input:a.prepare_input.write_text(json.dumps(cases,separators=(',',':'))+'\n');print(f'Prepared {len(cases)} cases')
    if not a.original_jsonl:
        if not a.prepare_input:p.error('Specify an input generation or original comparison')
        return
    execution_path=Path(str(a.original_jsonl)+'.json');report=json.loads(execution_path.read_text());verify_original(report,cases)
    originals=[json.loads(s) for s in a.original_jsonl.read_text().splitlines()]
    if len(originals)!=len(cases) or report['cases']!=len(cases):raise ValueError('Case count mismatch')
    exact=total=0;maximum=0.;goldens=[]
    with tempfile.TemporaryDirectory(prefix='adc-continuation-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            n,t,error=compare_case(native,item,expected);exact+=n;total+=t;maximum=max(maximum,error);goldens.append(dict(input=item,original=expected))
    report.update(all_results_match=True,exact_binary64_matches=exact,binary64_comparisons=total,maximum_absolute_error=maximum,native_compiler=compiler,native_flags=FLAGS,original_execution_jsonl_sha256=digest(a.original_jsonl),original_execution_report_sha256=digest(execution_path))
    if a.write_evidence:
        content=(json.dumps(dict(schema_version=1,input_sha256=IMAGE_SHA,cases=goldens),separators=(',',':'))+'\n').encode();fixture=ROOT/'tests/fixtures/adc-continuation-original.json.gz';fixture.write_bytes(gzip.compress(content,mtime=0))
        report.update(fixture_sha256=hashlib.sha256(content).hexdigest(),portable_golden_sha256=digest(fixture),portable_golden_cases=len(goldens),source_sha256={path:digest(ROOT/path) for path in SOURCES})
        (ROOT/'analysis/adc-continuation-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['cases','all_results_match','exact_binary64_matches','binary64_comparisons','maximum_absolute_error']},indent=2))
if __name__=='__main__':main()
