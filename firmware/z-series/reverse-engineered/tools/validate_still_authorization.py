#!/usr/bin/env python3
"""Compare PicoStill authorization request, receiver and JSON parsing with Z119."""
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


def input_cases():
    cases=[]
    def add(**v):cases.append(dict(id='auth-'+str(len(cases)),**v))
    for pending,configured,requested,cleaned,machine in itertools.product([False,True],[False,True],[False,True],[False,True],['fixture-machine',None,'','x'*130]):
        add(op='request',pending=pending,configured=configured,requested=requested,cleaned=cleaned,machine=machine,initial_body='old')
    for machine,still,cleaned in itertools.product(['m',None,'a'*440,'a'*500],['s',None,'b'*40],[False,True]):add(op='prepare',machine=machine,still=still,cleaned=cleaned)
    for content_type,length in itertools.product(['application/json','application/json; charset=utf-8','Application/json','application/json; charset=UTF-8','text/plain',''],[-1,0,511,512,513,2147483647]):add(op='headers',content_type=content_type,content_length=length,initial_body='old',frozen=True,truncated=True)
    for frozen,truncated,prefix,count in itertools.product([False,True],[False,True],[0,500,512],[0,1,12,13,512,514]):add(op='body',frozen=frozen,truncated=truncated,initial_body='p'*prefix,body='x'*count)
    for frozen,count in itertools.product([False,True],[0,1,512]):add(op='body',frozen=frozen,body=None,count=count)
    documents=['{}','null','[]','42','true','{"Other":1}','{"PicoStill":null}','{"PicoStill":{}}','{"PicoStill":{"UID":"abc","ProfileID":17,"CleanedAckDate":"2020-01-02"}}']
    for uid,date,profile in itertools.product(['',None,17,True,'u'*16,'u'*17],['',None,'d'*33],[None,-1,'-1',3.5]):
        documents.append(json.dumps({'PicoStill':{'UID':uid,'CleanedAckDate':date,'ProfileID':profile}},separators=(',',':')))
    documents += ['', '{', '{"PicoStill":', '{"PicoStill":{"UID":"parsed"},', '{"PicoStill":{"UID":"bad\\u0041"}}', "{PicoStill:{UID:'a\\q',ProfileID:1}}",'/* comment */ {"PicoStill":{"UID":"ok"}}','['*11+'0'+']'*11]
    for document in documents:add(op='parse',json=document)
    for pending,state,document in itertools.product([False,True],range(6),[documents[0],documents[8],'{']):add(op='tick',pending=pending,http_state=state,json=document)
    for document in ['{}',documents[8],'{','{"PicoStill":{"UID":"abc"}}']:
        add(op='roundtrip',chunks=[document[:5],document[5:]],content_length=len(document))
    add(op='complete',pending=True,initial_body='unchanged',frozen=True)
    return cases


def build(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));binary=Path(directory)/'still-authorization'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'include'),'-I',str(dependency()/'src'),str(ROOT/'tests/still_authorization_probe.cpp'),str(ROOT/'src/still_authorization.cpp'),str(ROOT/'adapters/arduinojson/still_authorization_json.cpp'),'-o',str(binary)],check=True)
    return binary,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]


def run(binary,cases):
    output=subprocess.run([str(binary)],input=''.join(json.dumps(v)+'\n' for v in cases),text=True,capture_output=True,check=True)
    rows=[json.loads(x) for x in output.stdout.splitlines()]
    if len(rows)!=len(cases):raise ValueError('Incomplete native output')
    return rows


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare-input',type=Path);p.add_argument('--original-jsonl',type=Path);p.add_argument('--export',type=Path,action='append',default=[]);p.add_argument('--write-evidence',action='store_true');args=p.parse_args();cases=input_cases();serialized=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if args.prepare_input:args.prepare_input.write_bytes(serialized);print('Prepared',len(cases),'Still authorization cases')
    if not args.original_jsonl:return
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(x) for x in args.original_jsonl.read_text().splitlines()]
    if execution['input_sha256']!=IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA:raise ValueError('Original image differs')
    if execution['input_file_sha256']!=hashlib.sha256(serialized).hexdigest():raise ValueError('Original input differs')
    if execution['cases']!=len(cases) or len(originals)!=len(cases):raise ValueError('Incomplete original run')
    with tempfile.TemporaryDirectory(prefix='z-still-auth-') as directory:binary,compiler=build(directory);native=run(binary,cases)
    for case,old,new in zip(cases,originals,native):
        if old!=new:raise ValueError('Still authorization mismatch '+case['id']+' fields='+repr([k for k in set(old)|set(new) if old.get(k)!=new.get(k)]))
    if args.write_evidence:
        if not args.export:p.error('--export required')
        output=io.BytesIO()
        with gzip.GzipFile(filename='',mode='wb',fileobj=output,mtime=0) as f:f.write((json.dumps(dict(input_sha256=IMAGE_SHA,cases=[dict(input=i,original=o) for i,o in zip(cases,originals)]),separators=(',',':'))+'\n').encode())
        fixture=ROOT/'tests/fixtures/still-authorization-original.json.gz';fixture.write_bytes(output.getvalue())
        paths=['include/zseries/still_authorization.hpp','src/still_authorization.cpp','adapters/arduinojson/still_authorization_json.cpp','platform/esp32/still_authorization_platform.hpp','platform/esp32/still_authorization_platform.cpp','tests/still_authorization_probe.cpp','tools/validate_still_authorization.py','tools/ghidra/EmulateStillAuthorization.java','tools/ghidra/Z119StillExecution.java']
        instructions={}
        for address in execution['original_functions_executed']:
            for export in args.export:
                path=export/'instructions'/('fn_'+address[2:]+'.json')
                if path.is_file():instructions[address]=digest(path);break
            else:raise ValueError('Missing export '+address)
        report=dict(schema_version=1,input_sha256=IMAGE_SHA,cases=len(cases),all_results_match=True,hardware_tested=False,complete_application_linked=False,compiler=compiler,source_sha256={p:digest(ROOT/p) for p in paths},golden_fixture_sha256=digest(fixture),original_execution_jsonl_sha256=digest(args.original_jsonl),original_instruction_sha256=instructions,**{k:execution[k] for k in ['original_functions_executed','modeled_boundaries']},scope='Original Still authorization request, JSON writer/parser, pending lifecycle, header/body/completion receivers and real returning assertion body. HTTP configure/POST, ROM memory/string operations, clock and diagnostic sink are explicit boundaries. Semantic diagnostics, not every formatted verbose log, are compared. Dedicated WiFiClientSecure binding is target-compiled; no live service or hardware execution.')
        (ROOT/'analysis/still-authorization-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All',len(cases),'Still authorization original/native cases match')


if __name__=='__main__':main()
