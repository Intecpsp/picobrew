#!/usr/bin/env python3
"""Compare Wifi.h orchestration against the preserved Z119 instructions."""
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

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = 'd84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
OPERATIONS = ['tick', 'transition', 'fail', 'target', 'connect', 'setup', 'timer_start', 'timer_advance']
WORDS = ['state','target','date','now','clock','rssi','level','resolved','active','attempts','deadline','duration','initial','maximum',
         'power_delay','disconnect_delay','connect_timeout','status','mode','scan_count','sdk_rssi','local','gateway','dns','dns_result',
         'success','request_ssid','request_password','request_internet','connected_ssid','connected_password','connected_internet',
         'assertion','argument','ap_name','present','old_local','old_gateway','old_dns']

def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def case(name, op='tick', **changes):
    values = dict(state=9,target=9,date=100,now=1000,clock=1003,rssi=-57,level=3,resolved=1,
                  active=0,attempts=3,deadline=1000,duration=120000,initial=60000,maximum=480000,
                  power_delay=500,disconnect_delay=1000,connect_timeout=20000,status=3,mode=1,scan_count=4,
                  sdk_rssi=-55,local=0x6401a8c0,gateway=0x0101a8c0,dns=0x08080808,dns_result=1,success=1,
                  request_ssid=1,request_password=1,request_internet=1,connected_ssid=1,connected_password=1,connected_internet=1,
                  argument=1,present=1,old_local=0x11223344,old_gateway=0x22334455,old_dns=0x33445566)
    values.update(changes)
    return dict(id=name,op=op,words=[values.get(key,0)&0xffffffff for key in WORDS])

def input_cases():
    cases=[]
    for previous in range(13):
        for target in range(13):
            cases.append(case(f'transition-{previous}-{target}','transition',state=previous,argument=target))
    for state in range(13):
        for target in range(13):
            cases.append(case(f'tick-route-{state}-{target}',state=state,target=target))
    for target in (0,1,2,3,4,9,11,0xffffffff):
        cases.append(case(f'target-{target}','target',argument=target,active=1))
    for state in (1,5,6,7,10):
        for now in (0,99,100,599,600,601,999,1000,1001,1099,1100,1101,20099,20100,20101,0xffffffff):
            for active in (0,1):
                cases.append(case(f'deadline-{state}-{now}-{active}',state=state,now=now,active=active,status=0))
    for status in (0,1,2,3,4,5,6,7,255):
        for now in (20099,20100,20101):
            for active in (0,1):
                cases.append(case(f'connect-status-{status}-{now}-{active}',state=7,status=status,now=now,active=active,deadline=20100))
    for value in range(256):
        cases.append(case(f'rssi-{value}',sdk_rssi=value))
    for count in (-2,-1,0,1,32767,32768,65535,65536,0x7fffffff):
        cases.append(case(f'scan-{count}',state=2,target=2,scan_count=count,active=1))
        cases.append(case(f'scan-enter-{count}','transition',state=2,argument=3,scan_count=count))
    for field in ('request_ssid','request_password','request_internet'):
        for value in range(2 if field.endswith('internet') else 4):
            cases.append(case(f'credential-{field}-{value}',**{field:value}))
    for ssid in range(4):
        for password in range(4):
            for internet in (0,1):
                cases.append(case(f'connect-{ssid}-{password}-{internet}','connect',target=1,active=1,
                                  request_ssid=ssid,request_password=password,request_internet=internet))
    for name in range(4):
        for success in (0,1):
            cases.append(case(f'ap-{name}-{success}','transition',argument=4,ap_name=name,success=success))
    for dns in (0,1,2):
        for rssi in (-128,-70,-69,-60,-59,-50,-49,-1,0,127):
            for address in (0,0x01020304):
                cases.append(case(f'checking-{dns}-{rssi}-{address}','transition',argument=8,dns_result=dns,dns=0 if not dns else 0x08080808,sdk_rssi=rssi,local=address))
    for resolved in (0,1):
        for expected in (0,1):
            cases.append(case(f'checked-{resolved}-{expected}',state=8,resolved=resolved,request_internet=expected))
    for error in range(7):
        for active in (0,1):
            for now in (999,1000,1001,0xffffffff):
                cases.append(case(f'fail-{error}-{active}-{now}','fail',state=7,argument=error,active=active,now=now))
    for op in ('timer_start','timer_advance'):
        for now in (0,1234,0xfffffff0):
            for duration in (0,1,240000,480000,0x7fffffff,0x80000000,0xffffffff):
                cases.append(case(f'{op}-{now}-{duration}',op,active=1,now=now,duration=duration,attempts=0xffffffff))
    for success in (0,1):cases.append(case(f'setup-{success}','setup',success=success))
    rng=random.Random(0x400f27e0)
    for index in range(192):
        cases.append(case(f'random-{index}',state=rng.randrange(13),target=rng.randrange(13),now=rng.randrange(2**32),date=rng.randrange(2**32),
                          status=rng.randrange(8),active=rng.randrange(2),deadline=rng.randrange(2**32),resolved=rng.randrange(2),
                          request_ssid=rng.randrange(4),request_password=rng.randrange(4),request_internet=rng.randrange(2),
                          connected_ssid=rng.randrange(4),connected_password=rng.randrange(4),connected_internet=rng.randrange(2)))
    return cases

def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));path=Path(directory)/'wifi.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-I',str(ROOT/'include'),
        str(ROOT/'src/wifi.cpp'),str(ROOT/'tests/wifi_bridge.cpp'),'-o',str(path)],check=True)
    native=ctypes.CDLL(str(path));u32=ctypes.POINTER(ctypes.c_uint32)
    native.wifi_test.argtypes=[ctypes.c_uint,u32,u32,u32,ctypes.c_size_t];native.wifi_test.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]

def native_case(native,item):
    words=(ctypes.c_uint32*len(WORDS))(*item['words']);result=(ctypes.c_uint32*31)();events=(ctypes.c_uint32*512)()
    status=native.wifi_test(OPERATIONS.index(item['op']),words,result,events,len(events))
    if status:raise AssertionError(f'Native fixture failed {item["id"]}: {status}')
    if result[1]>len(events) or result[1]%8:raise AssertionError('Invalid event shape')
    return dict(id=item['id'],result=result[0],state=list(result[2:]),events=[list(events[i:i+8]) for i in range(0,result[1],8)])

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path);parser.add_argument('--input',type=Path)
    parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    cases=json.loads(args.input.read_text()) if args.input else input_cases()
    if args.input and args.write_evidence:parser.error('Evidence requires complete deterministic cases')
    serialized=json.dumps(cases,separators=(',',':'))+'\n'
    if args.prepare_input:args.prepare_input.write_text(serialized);print(f'Prepared {len(cases)} Wifi cases')
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    input_hash=digest(args.input) if args.input else hashlib.sha256(serialized.encode()).hexdigest()
    if execution['input_sha256']!=IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Original image mismatch')
    if execution['input_file_sha256']!=input_hash or execution['cases']!=len(cases) or len(originals)!=len(cases):raise SystemExit('Original input/count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-wifi-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps(dict(case=item,original=expected,native=actual),indent=2));raise SystemExit('Wifi mismatch')
    paths=['include/zseries/wifi.hpp','src/wifi.cpp','tests/wifi_bridge.cpp','tests/test_wifi.py','tools/validate_wifi.py',
           'tools/ghidra/EmulateWifi.java','tools/ghidra/Z119Execution.java',
           'platform/esp32/wifi_platform.hpp','platform/esp32/wifi_platform.cpp','tools/build_esp32_objects.py']
    report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,compiler=compiler,hardware_tested=False,
                original_execution_jsonl_sha256=digest(args.original_jsonl),input_cases_sha256=input_hash,
                source_sha256={p:digest(ROOT/p) for p in paths},**{k:execution[k] for k in ['scope','original_functions_executed','modeled_boundaries']})
    if args.export:report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export:parser.error('Evidence requires --export')
        goldens=[dict(input=i,original=o) for i,o in zip(cases,originals) if not i['id'].startswith('random-')]
        fixture=ROOT/'tests/fixtures/wifi-original.json'
        # One complete scenario per line keeps exact evidence reviewable
        # without repeating indentation across thousands of numeric fields.
        fixture.write_text('{"input_sha256":'+json.dumps(IMAGE_SHA)+',"cases":[\n'+
                           ',\n'.join(json.dumps(row,separators=(',',':')) for row in goldens)+'\n]}\n')
        report['golden_cases']=len(goldens);report['golden_fixture_sha256']=digest(fixture)
        (ROOT/'analysis/wifi-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'All {len(cases)} original/native Wifi cases match')
if __name__=='__main__':main()
