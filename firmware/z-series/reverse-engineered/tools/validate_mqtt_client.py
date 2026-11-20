#!/usr/bin/env python3
"""Compare recovered MQTT wire packets and lifecycle with original Z119 execution."""
import argparse
import gzip
import hashlib
import io
import itertools
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from validate_display import IMAGE_SHA,digest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from program_recipe_native import dependency


def packet(header,payload):
    left=len(payload);length=[]
    while True:
        byte=left&127;left>>=7;length.append(byte|(128 if left else 0))
        if not left:break
    return [header]+length+list(payload)


def publish(topic,payload,qos=0,identifier=0x1234,retained=False):
    body=[len(topic)>>8,len(topic)&255]+list(topic.encode())
    if qos:body += [identifier>>8,identifier&255]
    return packet(0x30|(qos<<1)|int(retained),body+list(payload))


def input_cases():
    cases=[]
    def add(**v):cases.append(dict(id='mqtt-'+str(len(cases)),**v))
    for bound,connected,state in itertools.product([False,True],[False,True],[-4,-3,-2,-1,0,1,2,5]):add(op=0,bound=bound,connected=connected,state=state)
    for connected,state,clock in itertools.product([False,True],[-1,0],[1000,0xfffffff0]):add(op=1,connected=connected,state=state,clock=clock)
    credentials=[{},dict(username=None,password=None),dict(password=None),dict(username=None),dict(username='',password=''),dict(will_topic='last/will',will_message='offline'),dict(will_topic='last/will',will_message='offline',will_qos=1,will_retain=True)]
    for options,code in itertools.product(credentials,range(6)):add(op=2,connected=False,incoming=[0x20,2,0,code],**options)
    for result in [0,2,-1]:add(op=2,connected=False,connect_result=result)
    add(op=2,connected=True,state=0)
    add(op=2,connected=False,host=None,address=0x01020304,incoming=[0x20,2,0,0])
    for incoming in [[],[0x20],[0x20,1,0],[0x30,2,0,0]]:add(op=2,connected=False,incoming=incoming,clock_step=5000)
    for limit in [0,1,15,1000]:add(op=2,connected=False,incoming=[0x20,2,0,0],write_limit=limit,clock_step=1)
    for size,payload_size,retained,limit in itertools.product([0,1,16,1017,1018],[0,1,16],[False,True],[0,0xffffffff]):
        add(op=3,topic='t'*size,payload='p'*payload_size,retained=retained,write_limit=limit,state=0)
    for connected in [False,True]:add(op=3,connected=connected,state=0)
    for qos,size,identifier in itertools.product([0,1,2],[0,1,20,1015,1016],[0,65534,65535]):
        add(op=4,qos=qos,topic='t'*size,message_id=identifier,state=0)
    for connected,ping,now in itertools.product([False,True],[False,True],[14999,15000,15001,0xffffffff]):add(op=5,connected=connected,ping=ping,state=0,clock=now)
    add(op=5,state=0,clock=10,last_in=0xfffffff0,last_out=0xfffffff0)
    packets=[[0xc0,0],[0xd0,0],publish('topic',b'data'),publish('topic',b'data',1),publish('topic',b'data',2),publish('topic',bytes([0,1,255]),0,retained=True),packet(0x90,[0,1,0]),[0x20,2,0,0],publish('x',b'a'*1024)]
    for incoming,callback in itertools.product(packets,[False,True]):add(op=5,state=0,incoming=incoming,callback=callback,ping=True)
    for incoming in packets+[[0x30,128,128,128,128,128],[0x30,128,128,128,128,0,0,0],[],[0x30],[0x30,3,0],[-1,0]]:
        add(op=6,incoming=incoming,clock_step=5000)
    return cases


def build(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));binary=Path(directory)/'mqtt-client'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'include'),'-I',str(dependency()/'src'),str(ROOT/'tests/mqtt_client_probe.cpp'),str(ROOT/'src/mqtt_client.cpp'),'-o',str(binary)],check=True)
    return binary,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]


def run(binary,cases):
    output=subprocess.run([str(binary)],input=''.join(json.dumps(v)+'\n' for v in cases),text=True,capture_output=True,check=True)
    rows=[json.loads(x) for x in output.stdout.splitlines()]
    if len(rows)!=len(cases):raise ValueError('Incomplete native output')
    return rows


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare-input',type=Path);p.add_argument('--original-jsonl',type=Path);p.add_argument('--export',type=Path,action='append',default=[]);p.add_argument('--write-evidence',action='store_true');args=p.parse_args();cases=input_cases();serialized=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if args.prepare_input:args.prepare_input.write_bytes(serialized);print('Prepared',len(cases),'MQTT cases')
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(x) for x in args.original_jsonl.read_text().splitlines()]
    if execution['input_sha256']!=IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise ValueError('Original image differs')
    if execution['input_file_sha256']!=hashlib.sha256(serialized).hexdigest():raise ValueError('Original input differs')
    if execution['cases']!=len(cases) or len(originals)!=len(cases):raise ValueError('Incomplete original run')
    with tempfile.TemporaryDirectory(prefix='z-mqtt-') as directory:binary,compiler=build(directory);native=run(binary,cases)
    for case,old,new in zip(cases,originals,native):
        if old!=new:raise ValueError('MQTT mismatch '+case['id']+' fields='+repr([k for k in set(old)|set(new) if old.get(k)!=new.get(k)]))
    if args.write_evidence:
        if not args.export:p.error('--export required')
        output=io.BytesIO()
        with gzip.GzipFile(filename='',mode='wb',fileobj=output,mtime=0) as f:f.write((json.dumps(dict(input_sha256=IMAGE_SHA,cases=[dict(input=i,original=o) for i,o in zip(cases,originals)]),separators=(',',':'))+'\n').encode())
        fixture=ROOT/'tests/fixtures/mqtt-client-original.json.gz';fixture.write_bytes(output.getvalue())
        paths=['include/zseries/mqtt_client.hpp','src/mqtt_client.cpp','tests/mqtt_client_probe.cpp','tools/validate_mqtt_client.py','tools/ghidra/EmulateMqttClient.java','tools/ghidra/Z119MqttExecution.java','platform/esp32/azure_mqtt_platform.hpp','platform/esp32/azure_mqtt_platform.cpp']
        instructions={}
        for address in execution['original_functions_executed']:
            for export in args.export:
                path=export/'instructions'/('fn_'+address[2:]+'.json')
                if path.is_file():instructions[address]=digest(path);break
            else:raise ValueError('Missing export '+address)
        report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,hardware_tested=False,complete_application_linked=False,compiler=compiler,source_sha256={p:digest(ROOT/p) for p in paths},golden_fixture_sha256=digest(fixture),original_execution_jsonl_sha256=digest(args.original_jsonl),original_instruction_sha256=instructions,**{k:execution[k] for k in ['original_functions_executed','modeled_boundaries']},scope='Complete original MQTT CONNECT/CONNACK, publish, subscribe, keepalive, bounded packet drain, message callback and QoS1 ACK, disconnect and connection-loss bodies. Real Xtensa LOOP control is modeled explicitly in a dedicated helper; original instruction/data bodies execute. Socket methods, clock and ROM memory/string operations are boundaries. Azure uses the no-Stream path. Overlong unchecked CONNECT strings and malformed callback spans are deliberately rejected by reconstructed bounds guards and are outside exact-equivalence claims.')
        (ROOT/'analysis/mqtt-client-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All',len(cases),'MQTT original/native cases match')


if __name__=='__main__':main()
