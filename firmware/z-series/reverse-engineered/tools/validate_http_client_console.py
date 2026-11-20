#!/usr/bin/env python3
"""Compare the complete OEM live HTTP diagnostic without publishing resources."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from http_client_console_cases import cases
from http_client_console_native import build,execute
import recover_backend_credentials as resources

FIXTURE=ROOT/'tests/fixtures/http-client-console-original.json.gz'
REPORT=ROOT/'analysis/http-client-console-validation.json'
SOURCES=['include/zseries/http_client_console_tests.hpp','src/http_client_console_tests.cpp',
    'include/zseries/http_client.hpp','src/http_client.cpp','include/zseries/application_console.hpp',
    'include/zseries/diagnostics.hpp','src/diagnostics.cpp','tests/http_client_console_bridge.cpp',
    'tests/http_client_console_native.py','tests/http_client_console_cases.py','tests/run_http_client_console_emulation.py',
    'tests/test_http_client_console.py','tools/validate_http_client_console.py','tools/recover_backend_credentials.py',
    'tools/ghidra/EmulateHttpClientConsole.java','tools/ghidra/Z119Execution.java']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def serialized_inputs():return (json.dumps(cases(),separators=(',',':'))+'\n').encode()
def encoded(value):return (json.dumps(value,separators=(',',':'))+'\n').encode()
def load_fixture():
    fixture=json.loads(gzip.decompress(FIXTURE.read_bytes()));rows=[]
    for chunk in fixture['chunks']:
        path=FIXTURE.parent/chunk['path']
        if digest(path)!=chunk['sha256']:raise ValueError('Original HTTP fixture shard changed')
        data=json.loads(gzip.decompress(path.read_bytes()))
        if len(data)!=chunk['cases']:raise ValueError('Original HTTP fixture shard incomplete')
        rows.extend(data)
    return dict(original=rows,execution=fixture['execution'],tools=fixture['tools'])
def write_fixture(fixture):
    chunks=[]
    for start in range(0,len(fixture['original']),5):
        name='http-client-console-original-%02d.json.gz'%(start//5+1);path=FIXTURE.parent/name
        rows=fixture['original'][start:start+5];path.write_bytes(gzip.compress(encoded(rows),mtime=0))
        if path.stat().st_size>80000:raise ValueError('Original HTTP proof shard exceeds publication bound')
        chunks.append(dict(path=name,sha256=digest(path),cases=len(rows)))
    FIXTURE.write_bytes(gzip.compress(encoded(dict(execution=fixture['execution'],tools=fixture['tools'],chunks=chunks)),mtime=0))
def fail_contracts(fn):
    sample=cases()[0];checks=0
    for missing in range(1,13):
        if execute(fn,sample,missing=missing)['result']!=2:raise AssertionError('A missing diagnostic service did not fail explicitly')
        checks+=1
    for kind in range(1,10):
        for occurrence in (1,2,3):
            actual=execute(fn,sample,failure_kind=kind,failure_at=occurrence)
            expected=4 if kind==9 else 3
            if actual['result']!=expected:raise AssertionError('A rejected callback did not propagate: kind=%d occurrence=%d'%(kind,occurrence))
            checks+=1
    return checks
def verify_private_resources():
    original=(ROOT.parent/'oem/upstream/zseries_0_0_119.bin').read_bytes()
    from analyze_images import parse_image
    segments=parse_image(original)['segments'];texts=[]
    for address in (resources.AUTHORIZATION_ADDRESS,0x3f411b78,0x3f413124,0x3f413260,0x3f41332f):
        segment=next(s for s in segments if s['load_address']<=address<s['load_address']+s['size_bytes'])
        offset=segment['file_offset']+address-segment['load_address'];texts.append(original[offset:offset+512].split(b'\0',1)[0])
    documents=[(ROOT/name).read_bytes() for name in SOURCES if (ROOT/name).exists()]
    if FIXTURE.exists():documents.append(encoded(load_fixture()))
    if any(value in document for value in texts for document in documents):raise AssertionError('A private original HTTP resource leaked into public source or evidence')
    return len(texts)
def target(toolchain,output):
    compiler=toolchain/'bin/xtensa-esp32-elf-g++';obj=output/'http_client_console_tests.o'
    flags=['-std=gnu++11','-Os','-Wall','-Wextra','-Werror','-mlongcalls','-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti','-I'+str(ROOT/'include')]
    subprocess.run([str(compiler),*flags,'-c',str(ROOT/'src/http_client_console_tests.cpp'),'-o',str(obj)],check=True)
    return dict(compiler=subprocess.check_output([str(compiler),'--version'],text=True).splitlines()[0],
                compiler_sha256=digest(compiler),flags=[f if not f.startswith('-I') else '-I<module>/include' for f in flags],
                source_sha256=digest(ROOT/'src/http_client_console_tests.cpp'),object_sha256=digest(obj),object_bytes=obj.stat().st_size)
def sanitize(output):
    header=output/'sanitizer-cases.hpp';rows=[]
    for c in cases():rows.append('{ {'+','.join(map(str,c['words']))+'}, '+json.dumps(c['wire'])+' }')
    header.write_text('struct TestInput { std::uint32_t words[19];const char* wire; };\nstatic const TestInput Inputs[]={\n'+',\n'.join(rows)+'\n};\n')
    main=output/'sanitizer-main.cpp';main.write_text('''#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstdio>
#include "sanitizer-cases.hpp"
extern "C" int http_client_console_case(const std::uint32_t*,const char*,unsigned,unsigned,unsigned,std::uint32_t*,std::size_t);
int main(){std::vector<std::uint32_t> out(2000000);unsigned count=0;
for(const auto& item:Inputs){if(http_client_console_case(item.words,item.wire,0,0,0,out.data(),out.size())||out[1])return 1;++count;}
for(unsigned missing=1;missing<=12;++missing){if(http_client_console_case(Inputs[0].words,Inputs[0].wire,missing,0,0,out.data(),out.size())||out[1]!=2)return 2;++count;}
for(unsigned kind=1;kind<=9;++kind)for(unsigned occurrence=1;occurrence<=3;++occurrence){if(http_client_console_case(Inputs[0].words,Inputs[0].wire,0,kind,occurrence,out.data(),out.size())||out[1]!=(kind==9?4u:3u))return 3;++count;}
std::printf("%u live HTTP diagnostic sanitizer cases passed\\n",count);return 0;}
''')
    sources=[ROOT/('src/'+name+'.cpp') for name in ('http_client_console_tests','http_client','enums','diagnostics')]
    executable=output/'http-client-console-sanitized'
    subprocess.run(['g++','-std=c++11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
        '-I'+str(ROOT/'include'),'-I'+str(output/'generated'),'-I'+str(output),*map(str,sources),str(ROOT/'tests/http_client_console_bridge.cpp'),str(main),'-o',str(executable)],check=True)
    # This execution environment is ptrace-managed; LeakSanitizer cannot run
    # here. Address/undefined-behavior instrumentation remains fully enabled.
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    completed=subprocess.run([str(executable)],env=env,check=True,capture_output=True,text=True,timeout=120)
    return dict(passed=True,cases=len(cases())+39,output=completed.stdout.strip(),leak_sanitizer=False,
                leak_sanitizer_boundary='Unavailable in ptrace-managed execution environment')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--execution',type=Path)
    parser.add_argument('--toolchain',type=Path);parser.add_argument('--write-fixture',action='store_true');parser.add_argument('--write-report',action='store_true')
    parser.add_argument('--sanitize',action='store_true');parser.add_argument('--output',type=Path,default=ROOT/'build/http-console-native');args=parser.parse_args()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    if args.execution:
        original=[json.loads(line) for line in (args.execution/'original.jsonl').read_text().splitlines()]
        metadata=json.loads((args.execution/'original.jsonl.json').read_text());tools=json.loads((args.execution/'execution-tools.json').read_text())
        if metadata['input_file_sha256']!=hashlib.sha256(serialized_inputs()).hexdigest():raise ValueError('Original inputs changed')
        fixture=dict(original=original,execution=metadata,tools=tools)
    else:fixture=load_fixture();original=fixture['original'];metadata=fixture['execution']
    if metadata['input_sha256']!=resources.IMAGE_SHA or metadata['cases']!=len(cases()):raise ValueError('Original execution identity/count mismatch')
    fn=build(output);actual=[execute(fn,item) for item in cases()]
    for expected,observed in zip(original,actual):
        if expected!=observed:raise AssertionError('Complete original/native comparison failed for '+observed['id']+'; traces contain hashes only')
    if len(original)!=len(actual):raise ValueError('Incomplete original trace')
    contracts=fail_contracts(fn)
    if args.write_fixture:write_fixture(fixture)
    privacy=verify_private_resources();target_result=target(args.toolchain.resolve(),output) if args.toolchain else None
    sanitized=sanitize(output) if args.sanitize else None
    counts={str(kind):sum(event[0]==kind for row in actual for event in row['events']) for kind in (0,5,9,16,17,18,22,23,24)}
    report=dict(schema_version=1,original_image_sha256=resources.IMAGE_SHA,original_entry='0x40110f80',cases=len(actual),
        ordered_events=sum(len(row['events']) for row in actual),event_counts=counts,contract_checks=contracts,
        private_resource_leak_checks=privacy,fixture_sha256=digest(FIXTURE),fixture_uncompressed_sha256=hashlib.sha256(gzip.decompress(FIXTURE.read_bytes())).hexdigest(),
        fixture_chunks_sha256={chunk['path']:chunk['sha256'] for chunk in json.loads(gzip.decompress(FIXTURE.read_bytes()))['chunks']},
        inputs_sha256=hashlib.sha256(serialized_inputs()).hexdigest(),source_sha256={name:digest(ROOT/name) for name in SOURCES},
        original_execution=metadata,target_compile=target_result,sanitizers=sanitized,passed=True,
        scope='Complete original live HTTP self-test, original response Stream, eight real TestScopes, debug scopes, returning assertions and watchdog/delay continuation; every ordered socket and formatted diagnostic byte sequence compared as SHA-256. Explicit SDK/socket/libc boundaries; no live OEM network requests or physical qualification.')
    if args.write_report:REPORT.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(cases=len(actual),ordered_events=report['ordered_events'],contract_checks=contracts,passed=True)))
if __name__=='__main__':main()
