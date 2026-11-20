#!/usr/bin/env python3
"""Original-byte acceptance for Core command routing and program-start metadata."""
from __future__ import annotations
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import validate_core_flow as common
ROOT=common.ROOT
KINDS=['command','program','coffee','boiler']
EXTRA=['program_id','temperature_bits','duration','location','still_id','command_ssid','command_password','boiler','program_source','resumed_session',
       'raw_hex','raw_wort','raw_drain','raw_ambient','adc_type','buffer_type','buffer_unique_id','buffer_has_name','buffer_parameter_id','coffee_program_id',
       'imperial','queued_program_id','manual_location','manual_byte','manual_bytes','pid_sample_time','pid_reverse','coffee_success']
SOURCES=['core_commands','core_flow','eeprom','heater','temperature','hex_detection','program_repository','program_catalog','program_writer',
         'program_runtime','program_runtime_execution','runtime_instruction','quantity']

def detector_temperatures(raw):
    # This previously accepted calibration is a declared Core-test boundary;
    # its separate exhaustive original-byte validation is temperature-validation.json.
    image=(ROOT.parent/'oem/upstream/zseries_0_0_119.bin').read_bytes()
    values=[]
    for channel,kind in ((3,2),(1,1),(2,1),(0,0),(0,3)):
        _,a,b,c,r=struct.unpack_from('<I4xdddd',image,0x287f0+kind*40)
        logarithm=math.log((4095.0/raw[channel]-1)*r)
        values.append(1/(logarithm*b+a+((logarithm*c)*logarithm)*logarithm)-273.15)
    return values

def case(name,kind='command',core=None,values=(91000.0,96.5),**changes):
    defaults=dict(program_id=1,temperature_bits=struct.unpack('<I',struct.pack('<f',67.375))[0],duration=123,location=4,still_id=42,
                  command_ssid=1,command_password=1,raw_hex=1500,raw_wort=2000,raw_drain=2000,raw_ambient=1500,adc_type=2,
                  buffer_type=6,buffer_unique_id=77,buffer_has_name=1,buffer_parameter_id=28,coffee_program_id=22,queued_program_id=99,
                  manual_location=3,manual_byte=1,manual_bytes=0x04030201,pid_sample_time=100)
    defaults.update(changes);defaults['coffee_success']=20<=defaults['coffee_program_id']<=23
    item=common.case(name,'tick',**(core or {}));item['kind']=kind
    item['extra']=[int(defaults.get(key,0))&0xffffffff for key in EXTRA];item['values']=list(values)
    item['detector_temperatures']=detector_temperatures(item['extra'][10:14]);return item

def input_cases():
    result=[]
    for state in range(19):
        for command in list(range(57))+[0xffffffff]:
            result.append(case(f'command-{state}-{command}',core=dict(state=state,command_id=command,runtime_start_pending=1)))
    for program in list(range(32))+[0xffffffff]:
        for flags in range(12):
            result.append(case(f'program-{program}-{flags}','program',core=dict(state=15 if flags&8 else 12,offline=flags&1,
                          config_still_mode=(flags>>1)%3,config_azure=(flags>>2)%3),program_id=program,program_source=flags%4,
                          buffer_has_name=flags&1,values=(91000+flags*100,96.5+flags/8)))
    for state in (0,11,13,14,16):
        result.append(case(f'program-assert-state-{state}','program',core=dict(state=state),program_id=7))
    for command in (1,2,46,51,54):
        for mask in (0,4,63):
            result.append(case(f'queue-{command}-{mask}',core=dict(state=12,command_id=command,queue_mask=mask)))
    for error in (0,1,0x10,0xfc,0xffffffff):
        for msp in (0,1,2,3):result.append(case(f'resume-{msp}-{error}',core=dict(state=14,command_id=28,msp_id=msp,msp_errors=error)))
    for command in (33,34,35,54):
        for value in (0,1,0x80000000,0xffffffff):
            result.append(case(f'runtime-payload-{command}-{value}',core=dict(state=14,command_id=command),duration=value,location=value,manual_location=value))
    for state in (5,12,14):
        for flags in range(8):
            result.append(case(f'wifi-command-{state}-{flags}',core=dict(state=state,command_id=42,service_mode=flags&1),command_ssid=(flags>>1)&1,command_password=(flags>>2)&1))
    for command in (0,2,44,45):
        for boiler in (0,1):
            for reverse in (0,1):
                result.append(case(f'boiler-{command}-{boiler}-{reverse}','boiler',core=dict(command_id=command),boiler=boiler,pid_reverse=reverse,pid_sample_time=250 if reverse else 100))
    for program in range(30):
        for imperial in (0,1):
            result.append(case(f'coffee-{program}-{imperial}','coffee',core=dict(command_id=2),coffee_program_id=program,imperial=imperial))
    result.append(case('coffee-wrong-command','coffee',core=dict(command_id=0),coffee_program_id=23))
    for raw in ((1500,2000,2000,1500),(350,2000,2000,1500),(2000,3000,2000,1500),(2000,2000,3000,1500),(3000,3000,3000,3000),(15,15,15,15),(4094,4094,4094,4094)):
        result.append(case('detector-'+'-'.join(map(str,raw)),core=dict(state=12,command_id=43),**dict(zip(EXTRA[10:14],raw))))
    return result

def smoke_cases(cases):
    return [c for i,c in enumerate(cases) if c['id'].startswith(('command-12-','command-14-','boiler-','coffee-')) or (c['kind']=='program' and c['id'].endswith('-0')) or i%97==0]

def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));directory=Path(directory)
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-O2','-fPIC','-I',str(ROOT/'include'),'-c',str(ROOT/'src/msp_protocol.c'),'-o',str(directory/'msp.o')],check=True)
    library=directory/'core-commands.so'
    subprocess.run(compiler+common.FLAGS+['-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),str(ROOT/'tests/core_commands_bridge.cpp')]+
                   [str(ROOT/f'src/{name}.cpp') for name in SOURCES]+[str(directory/'msp.o'),'-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));word=ctypes.POINTER(ctypes.c_uint32);size=ctypes.POINTER(ctypes.c_size_t);text=ctypes.POINTER(ctypes.c_char)
    native.core_commands_action.argtypes=[ctypes.c_uint,word,word,ctypes.POINTER(ctypes.c_double),word,size,text,word,text,word,size,ctypes.c_int]
    native.core_commands_action.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]

def native_case(native,item,missing=False):
    word=ctypes.c_uint32;words=(word*64)(*item['words']);extra=(word*40)(*item['extra']);values=(ctypes.c_double*2)(*item['values']);events=(word*2048)();count=ctypes.c_size_t()
    credentials=ctypes.create_string_buffer(260);session=(word*27)();name=ctypes.create_string_buffer(65);posted=(word*288)();posted_count=ctypes.c_size_t()
    status=native.core_commands_action(KINDS.index(item['kind']),words,extra,values,events,ctypes.byref(count),credentials,session,name,posted,ctypes.byref(posted_count),missing)
    if count.value>len(events) or count.value%4 or posted_count.value>len(posted) or posted_count.value%18:raise AssertionError('Invalid trace sizes')
    return status,dict(id=item['id'],words=list(words),events=[list(events[i:i+4]) for i in range(0,count.value,4)],
        credentials=[credentials.raw[i*65:(i+1)*65].split(b'\0',1)[0].decode() for i in range(4)],extra=list(extra),session=list(session),name=name.value.decode(),
        runtime_commands=[list(posted[i:i+18]) for i in range(0,posted_count.value,18)])

def compare_case(native,item,expected):
    status,actual=native_case(native,item)
    if status or actual!=expected:
        differences={key:(actual.get(key),expected.get(key)) for key in actual if actual.get(key)!=expected.get(key)}
        raise AssertionError(f"Core command mismatch {item['id']} status {status}: {differences}")
    return len(actual['events'])

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path);parser.add_argument('--original-jsonl',type=Path)
    parser.add_argument('--smoke',action='store_true');parser.add_argument('--write-evidence',action='store_true');parser.add_argument('--export',type=Path)
    args=parser.parse_args();cases=input_cases();cases=smoke_cases(cases) if args.smoke else cases
    if args.prepare_input:args.prepare_input.write_text(json.dumps(cases,separators=(',',':'))+'\n');print(f'Prepared {len(cases)} Core command scenarios')
    if not args.original_jsonl:return 0
    report_path=Path(str(args.original_jsonl)+'.json');execution=json.loads(report_path.read_text());originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if execution['input_sha256']!=common.IMAGE_SHA or execution['cases']!=len(cases) or len(originals)!=len(cases):raise ValueError('Incomplete original comparison')
    events=0;goldens=[]
    with tempfile.TemporaryDirectory(prefix='zseries-core-commands-') as directory:
        native,compiler=load_native(directory)
        for i,(item,expected) in enumerate(zip(cases,originals)):
            events+=compare_case(native,item,expected)
            if i%7==0 or item['id'].startswith(('runtime-payload-','wifi-command-','boiler-','detector-','queue-','program-assert-','coffee-wrong')):goldens.append(dict(input=item,original=expected))
    report=dict(execution,all_results_match=True,service_call_events=events,portable_golden_cases=len(goldens),native_compiler=compiler,native_flags=common.FLAGS,
                original_execution_jsonl_sha256=common.digest(args.original_jsonl),original_execution_report_sha256=common.digest(report_path))
    if args.write_evidence:
        if args.smoke or not args.export:parser.error('Evidence requires full inputs and instruction export')
        report['instruction_export_sha256']={a:common.digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in execution['original_functions_executed']}
        files=['include/zseries/core_commands.hpp','src/core_commands.cpp','include/zseries/core_flow.hpp','src/core_flow.cpp','tests/core_commands_bridge.cpp','tests/core_flow_fixture.hpp',
               'tools/validate_core_commands.py','tools/ghidra/EmulateCoreCommands.java','tools/ghidra/EmulateCoreFlow.java','tools/ghidra/Z119Execution.java']
        report['source_sha256']={p:common.digest(ROOT/p) for p in files}
        report['accepted_dependency_reports_sha256']={p:common.digest(ROOT/'analysis'/p) for p in ['program-repository-validation.json','heater-validation.json','temperature-validation.json','hex-detection-validation.json']}
        fixture=ROOT/'tests/fixtures/core-commands-original.json';fixture.write_text(json.dumps(dict(schema_version=1,input_sha256=common.IMAGE_SHA,words=common.WORDS,extra=EXTRA,cases=goldens),separators=(',',':'))+'\n')
        report['portable_golden_sha256']=common.digest(fixture);(ROOT/'analysis/core-commands-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ('cases','all_results_match','service_call_events','portable_golden_cases')},indent=2))
    return 0
if __name__=='__main__':raise SystemExit(main())
