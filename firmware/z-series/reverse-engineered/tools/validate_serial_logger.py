#!/usr/bin/env python3
"""Replay complete Z119 SerialLogger and Core CSV calls against recovered source."""
import argparse
import copy
import gzip
import hashlib
import io
import itertools
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import sys
import tempfile
from validate_display import IMAGE_SHA, digest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from program_recipe_native import dependency


def input_cases():
    cases=[]
    def add(**value):
        cases.append(dict(id='csv-'+str(len(cases)),**value))
    for op,size,formatted in itertools.product(['header','data','out','info'],[0,1,383,384,385,700],[False,True]):
        add(op=op,fixture=True,fill='x'*size,formatted=formatted,ever=len(cases)%3==0)
    for enabled,value in itertools.product([False,True],repeat=2):
        add(op='enable',enabled=enabled,value=value,fixture=True,fill='header')
    for enabled,(now,deadline),interval in itertools.product([False,True],[(0,0),(99,100),(100,100),(101,100),(0,0xffffffff),(0xffffffff,0)],[0,200,0xffffffff]):
        add(op='tick',fixture=True,fill='row',enabled=enabled,now=now,deadline=deadline,interval=interval)
    for value in ['', '0', '199', '200', '201', '  +300ms', '-1', '-200', '2147483647', '4294967295', '-2147483649', 'garbage', '\t42\n']:
        add(op='interval',argument=value)
    for op,frozen,truncated,size in itertools.product(['append','format'],[False,True],[False,True],[0,380,384,385]):
        add(op=op,prefix='seed',argument='a'*size,frozen=frozen,truncated=truncated)
    add(op='format',prefix='seed',argument='ignored',null_format=True,frozen=True)
    add(op='console')
    d=[67.2,48.3,-12.4,65.31,0.13,92.62,-0.27,65.43,94.76,22.12,20.21,101325.,101325.,100.,65.3,66.4,67.6,68.7,1.23,12.7,2.6,7.9]
    w=[1,0x12345678,8,0x30,4,0,12,17,1234,567,0,123,4,30,12,9]
    add(op='header',d=d,w=w)
    for pumps,valve,moving in itertools.product(range(16),[0,1,9,10,255],[0,1]):
        words=w[:];words[3]=pumps*16+7;words[4]=valve;words[5]=moving;words[10]=pumps*4;words[11]=0xffffffff;words[12]=0xffffffff
        add(op='out',d=d,w=words,step='Mash')
    for step in ['', 'x'*64, 'x'*160, 'x'*384, 'x'*1000]:
        add(op='out',d=d,w=w,step=step)
    rng=random.Random(119556064)
    for _ in range(32):
        values=[rng.uniform(-90,120) for _ in range(22)];values[11]=rng.uniform(70000,110000);values[12]=101325.;values[13]=rng.uniform(85,105)
        for i in [19,20,21]:values[i]=rng.uniform(0,100)
        words=w[:];words[0]=rng.randrange(2);words[8]=rng.randrange(2**32);words[9]=rng.randrange(2**32);words[10]=rng.randrange(62);words[11]=rng.randrange(2**32)
        words[13:16]=[rng.randrange(35) for _ in range(3)]
        add(op='out',d=values,w=words)
    large=d[:];large[0:3]=[1e100,-1e100,1e100];add(op='out',d=large,w=w)
    return cases


def build(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));output=Path(directory)/'serial-logger'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off',
        '-I',str(ROOT/'include'),'-I',str(dependency()/'src'),str(ROOT/'tests/serial_logger_probe.cpp'),
        str(ROOT/'src/serial_logger.cpp'),str(ROOT/'src/core_csv.cpp'),str(ROOT/'src/console.cpp'),str(ROOT/'src/enums.cpp'),'-o',str(output)],check=True)
    return output,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]


def run(binary,cases):
    output=subprocess.run([str(binary)],input=''.join(json.dumps(c)+'\n' for c in cases),text=True,capture_output=True,check=True)
    result=[json.loads(line) for line in output.stdout.splitlines()]
    if len(result)!=len(cases):raise ValueError('Incomplete native output')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare-input',type=Path);p.add_argument('--original-jsonl',type=Path);p.add_argument('--export',type=Path,action='append',default=[]);p.add_argument('--write-evidence',action='store_true');args=p.parse_args()
    cases=input_cases();serialized=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if args.prepare_input:args.prepare_input.write_bytes(serialized);print('Prepared',len(cases),'SerialLogger/CSV cases')
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(x) for x in args.original_jsonl.read_text().splitlines()]
    if execution['input_sha256']!=IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise ValueError('Original image identity differs')
    if execution['input_file_sha256']!=hashlib.sha256(serialized).hexdigest():raise ValueError('Original input differs')
    if len(originals)!=len(cases) or execution['cases']!=len(cases):raise ValueError('Incomplete original execution')
    with tempfile.TemporaryDirectory(prefix='z-serial-logger-') as directory:
        binary,compiler=build(directory);native=run(binary,cases)
    for case,old,new in zip(cases,originals,native):
        if old!=new:raise ValueError('SerialLogger mismatch '+case['id']+' fields='+repr([k for k in set(old)|set(new) if old.get(k)!=new.get(k)]))
    if args.write_evidence:
        if not args.export:p.error('--export required for instruction identity')
        buf=io.BytesIO()
        with gzip.GzipFile(filename='',mode='wb',fileobj=buf,mtime=0) as f:f.write((json.dumps(dict(input_sha256=IMAGE_SHA,cases=[dict(input=i,original=o) for i,o in zip(cases,originals)]),separators=(',',':'))+'\n').encode())
        fixture=ROOT/'tests/fixtures/serial-logger-original.json.gz';fixture.write_bytes(buf.getvalue())
        sources=['include/zseries/serial_logger.hpp','src/serial_logger.cpp','src/core_csv.cpp','tests/serial_logger_probe.cpp','tools/validate_serial_logger.py','tools/ghidra/EmulateSerialLogger.java','tools/ghidra/Z119Execution.java']
        instructions={}
        for address in execution['original_functions_executed']:
            for export in args.export:
                path=export/'instructions'/('fn_'+address[2:]+'.json')
                if path.is_file():instructions[address]=digest(path);break
            else:raise ValueError('Missing instruction export '+address)
        report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,hardware_tested=False,complete_application_linked=False,compiler=compiler,source_sha256={f:digest(ROOT/f) for f in sources},golden_fixture_sha256=digest(fixture),original_execution_jsonl_sha256=digest(args.original_jsonl),original_instruction_sha256=instructions,**{k:execution[k] for k in ['original_functions_executed','modeled_boundaries']},scope='Full original SerialLogger lifecycle, Core CSV header and all five data formatter bodies, bounded append/printf and real returning assertion. SDK printf/ROM atoi/libm, logging, clock and Shell registration are explicit modeled boundaries. The printf model decodes the Xtensa va_list ABI and rounds finite binary doubles to even; live target libc/UART execution remains outside this comparison.')
        (ROOT/'analysis/serial-logger-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All',len(cases),'SerialLogger/CSV original and native cases match')


if __name__=='__main__':main()
