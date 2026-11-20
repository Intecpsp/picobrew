#!/usr/bin/env python3
"""Accept independently executed Z119 StillClient and sensor protocol evidence."""
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
OPERATIONS=['host','uri','reset','command','configure','tick','error','headers','body','complete','parse','speeds','stats','encode','has_error','error_text']
WORDS=['pending','error','retryable','done','http_state','http_error','socket_error','configure_result','get_result','post_result','status','length','now',
       'old_timestamp','old_delta','window10','window60','state','poll_stop','argument','capacity','repeat','seed']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def case(name,op='complete',text='#32.0,68.0,212.0,50.0,14.7,1,0,1,None#',password='',**changes):
    values=dict(pending=9,error=0,retryable=0,done=0,http_state=0,http_error=0,socket_error=0,configure_result=1,get_result=1,post_result=1,
        status=200,length=-1,now=2000,old_timestamp=100,old_delta=75,window10=1,window60=1,state=17,poll_stop=0,argument=1,capacity=64,repeat=1,seed=3)
    values.update(changes);return dict(id=name,op=op,words=[values[key]&0xffffffff for key in WORDS],text=text,password=password)
def input_cases():
    rows=[]
    for text in ('','1','192.168.1.73','1'*16,'1'*17,'a'*33):rows.append(case('host-'+str(len(text))+'-'+text,'host',text=text))
    for command in range(14):
        rows.append(case(f'uri-{command}','uri',argument=command))
        for state,pending,configure,get in itertools.product((0,3),(0,9),(0,1),(0,1)):
            rows.append(case(f'command-{command}-{state}-{pending}-{configure}-{get}','command',argument=command,http_state=state,pending=pending,configure_result=configure,get_result=get,http_error=10,socket_error=113))
    for pending in (0,4):rows.append(case(f'reset-{pending}','reset',pending=pending,error=1,retryable=1,done=1))
    for error,socket,existing in itertools.product((0,1,6,9,10,11),(0,103,104,112,113,127,128,129),(0,1)):
        for op in ('tick','error'):rows.append(case(f'{op}-{error}-{socket}-{existing}',op,http_error=error,socket_error=socket,error=existing,retryable=existing))
    for ssid,password in (('',''),('home','password'),('My Network','p@ss/word!'),('a'*32,'b'*64),('a-b_c.d','x+y=z'),('line\nname','cr\rpassword')):
        for configure,post,pending in itertools.product((0,1),(0,1),(0,9)):
            rows.append(case(f'configure-{ssid!r}-{configure}-{post}-{pending}','configure',text=ssid,password=password,configure_result=configure,post_result=post,pending=pending,http_error=10))
    for status in (-1,0,1,200,404,500):
        for length in (-1,0,256,512):rows.append(case(f'headers-{status}-{length}','headers',argument=status,length=length,text='text/plain'))
    for length in (0,1,243,244,245,255,256,257,512):rows.append(case(f'body-{length}','body',text='x'*length))
    bodies=['','##','#x#','x#x','#bad','##x##','#0.0.0.0#','#192.168.1.73#','#'+'x'*31+'#','#'+'x'*32+'#','#'+'x'*64+'#',
            '#32.0,68.0,212.0,50.0,14.7,1,0,1,None#','#32,68,212,50,14.7,1,0,1,Overheat#']
    for command,status,body in itertools.product(range(13),(0,200,201,404,500),range(len(bodies))):
        rows.append(case(f'complete-{command}-{status}-{body}',pending=command,status=status,text=bodies[body]))
    numeric=['0','0.','0.0','0.00','-0.0','+0.0','1','-1','-40','100.25','1.2e2','1junk','junk','','.0',' 32.0','1e-300','123456789012345','1234567890123456']
    for position,value in itertools.product(range(5),numeric):
        values=['32','68','212','50','14.7'];values[position]=value
        rows.append(case(f'number-{position}-{numeric.index(value)}','parse',text='#'+','.join(values)+',1,0,1,None#'))
    flags=['0','00','1','False','','true']
    for indexes in itertools.product(range(len(flags)),repeat=3):
        rows.append(case('flags-'+'-'.join(map(str,indexes)),'parse',text='#32,68,212,50,14.7,'+','.join(flags[i] for i in indexes)+',None#'))
    for text in ('#1,2,3,4,5#','#1,2,3,4,5,1,1,1#','#1,2,3,4,5,1,1,1,#','#1,2,3,4,5,1,1,1,None,extra#','#1,2,3,4,5,1,1,1,'+'x'*63+'#',
                 '#1,2,3,4,5,1,1,1,'+'x'*64+'#','#1,2,3,4,5,'+'1'*16+',0,0,None#'):
        rows.append(case(f'parse-framing-{len(rows)}','parse',text=text))
    for old,now in itertools.product((0,1,100,0xfffffff0),(0,1,99,100,1000,0xffffffff)):
        rows.append(case(f'speeds-{old}-{now}','speeds',old_timestamp=old,now=now))
    for repeat,seed,now in itertools.product((0,1,2,29,30,31,60,61),(0,3,123),(0,1,1000,0xffff0000)):
        rows.append(case(f'stats-{repeat}-{seed}-{now}','stats',repeat=repeat,seed=seed,now=now))
    for byte,capacity in itertools.product(range(1,256),(0,1,2,3,4,5,16,64)):
        rows.append(case(f'encode-byte-{byte}-{capacity}','encode',text=chr(byte),capacity=capacity))
    for text,capacity in itertools.product(('a'*127,' '*30,'a'*60+' ','x\ny\rz','a-b_c.~','1234567890&abc'),(1,3,4,5,63,64,127,128)):
        rows.append(case(f'encode-buffer-{len(rows)}','encode',text=text,capacity=capacity))
    for state,stopped,message in itertools.product(range(21),(0,1),('','fixture error')):
        for op in ('has_error','error_text'):rows.append(case(f'{op}-{state}-{stopped}-{bool(message)}',op,text=message,state=state,poll_stop=stopped))
    rng=random.Random(0x401009bc)
    for index in range(128):
        numbers=[str(rng.randrange(-4000,4000)/4.0) for _ in range(5)]
        rows.append(case(f'random-{index}','parse',text='#'+','.join(numbers)+',1,0,1,None#',now=rng.randrange(2**32),old_timestamp=rng.randrange(2**32)))
    return rows
def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));path=Path(directory)/'still_client.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),
        str(ROOT/'src/still_client.cpp'),str(ROOT/'src/pico_still_sensors.cpp'),str(ROOT/'src/enums.cpp'),str(ROOT/'tests/still_client_bridge.cpp'),'-o',str(path)],check=True)
    native=ctypes.CDLL(str(path));u32=ctypes.POINTER(ctypes.c_uint32);native.still_client_test.argtypes=[ctypes.c_uint,u32,ctypes.c_char_p,ctypes.c_char_p,u32,ctypes.c_size_t];native.still_client_test.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]
def native_case(native,item):
    words=(ctypes.c_uint32*len(WORDS))(*item['words']);output=(ctypes.c_uint32*12000)()
    status=native.still_client_test(OPERATIONS.index(item['op']),words,item['text'].encode('latin1'),item['password'].encode('latin1'),output,len(output))
    if status:raise AssertionError(f'Native fixture failed {item["id"]}: {status}')
    length=output[2];offset=4+length;events=[]
    for _ in range(output[3]):n=output[offset];events.append(list(output[offset+1:offset+1+n]));offset+=n+1
    if offset!=output[0]+1:raise AssertionError('Bad fixture shape')
    return dict(id=item['id'],result=output[1],state=list(output[4:4+length]),events=events)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path);parser.add_argument('--input',type=Path)
    parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    cases=json.loads(args.input.read_text()) if args.input else input_cases()
    if args.input and args.write_evidence:parser.error('Evidence requires complete deterministic cases')
    serialized=json.dumps(cases,separators=(',',':'))+'\n'
    if args.prepare_input:args.prepare_input.write_text(serialized);print(f'Prepared {len(cases)} StillClient/protocol cases')
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    input_hash=digest(args.input) if args.input else hashlib.sha256(serialized.encode()).hexdigest()
    if execution['input_sha256']!=IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise SystemExit('Original image mismatch')
    if execution['input_file_sha256']!=input_hash or execution['cases']!=len(cases) or len(originals)!=len(cases):raise SystemExit('Original input/count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-still-client-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps(dict(case=item,original=expected,native=actual),indent=2));raise SystemExit('StillClient/protocol mismatch')
    paths=['include/zseries/still_client.hpp','src/still_client.cpp','include/zseries/pico_still_sensors.hpp','src/pico_still_sensors.cpp',
        'tests/still_client_bridge.cpp','tests/test_still_client.py','tools/validate_still_client.py','tools/ghidra/EmulateStillClient.java','tools/ghidra/Z119Execution.java']
    report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,compiler=compiler,hardware_tested=False,
        original_execution_jsonl_sha256=digest(args.original_jsonl),input_cases_sha256=input_hash,source_sha256={p:digest(ROOT/p) for p in paths},
        **{key:execution[key] for key in ['scope','original_functions_executed','modeled_boundaries']})
    if args.export:report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export:parser.error('Evidence requires --export')
        goldens=[dict(input=i,original=o) for i,o in zip(cases,originals) if not i['id'].startswith('random-')]
        fixture=ROOT/'tests/fixtures/still-client-original.json.gz';payload=json.dumps(dict(input_sha256=IMAGE_SHA,cases=goldens),separators=(',',':'))+'\n';fixture.write_bytes(gzip.compress(payload.encode(),mtime=0))
        report['golden_cases']=len(goldens);report['golden_fixture_sha256']=digest(fixture);(ROOT/'analysis/still-client-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'All {len(cases)} original/native StillClient/protocol cases match')
if __name__=='__main__':main()
