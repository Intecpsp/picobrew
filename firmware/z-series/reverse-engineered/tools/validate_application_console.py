#!/usr/bin/env python3
"""Replay recorded original component registries/callbacks against composed source."""
from pathlib import Path
import argparse
import collections
import gzip
import hashlib
import json
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from application_console_cases import command_cases
from application_console_native import build,run,project,registry_tree
from validate_backend_console import guarded_original,GUARDS

def validate_basic_dispatch(binary):
    cases=[dict(direct_callback=0x4011ed44,direct_argument='',diagnostic_aggregate=True),dict(direct_shell='test',direct_token='basic',direct_argument='',diagnostic_aggregate=True)]
    original=json.loads(gzip.decompress((ROOT/'tests/fixtures/basic-console-original.json.gz').read_bytes()))['cases'][148]
    expected=[GUARDS.get(event,event)for event in original['events']]
    for actual in run(binary,cases):
        events=[]
        for event in actual['effects']:
            if event[0]=='scope':event[1]=int(event[1])
            if event[0]=='platform':events.append('invoke|0x'+format(event[1],'08x')+'|'+event[2])
            else:events.append('|'.join(str(value)for value in event))
        if actual['result']!=0 or events!=expected or [actual['state']['counter'],actual['state']['clock']]!=original['state']:raise RuntimeError('Basic body/wrapper dispatch differs')
    return len(cases)

def validate_network_dispatch(binary):
    from network_console_cases import cases as network_cases
    original=json.loads(gzip.decompress((ROOT/'tests/fixtures/network-console-original.json.gz').read_bytes()))['cases']
    inputs=network_cases();cases=[];indices=[]
    for index in range(3,16):
        case=dict(inputs[index],network_diagnostic=True,direct_argument='')
        operation=case['operation']
        if operation in(1,3):case['direct_callback']={1:0x4011bffc,3:0x4010a364}[operation]
        else:case.update(direct_shell='test',direct_token={2:'net',4:'int',5:'all'}[operation])
        cases.append(case);indices.append(index)
    for index,actual in zip(indices,run(binary,cases)):
        events=[]
        for event in actual['effects']:
            if event[0]=='platform':events.append('invoke|'+str(event[1]))
            elif event[0]=='write':events.append('write|'+event[1].encode().hex())
            else:events.append('|'.join(str(value)for value in event))
        state=[actual['state'][key]for key in('counter','clock','network')]
        if actual['result']!=0 or dict(state=state,events=events)!=original[index]:raise RuntimeError('Network/Int/All body/registry dispatch differs in '+str(index))
    return len(cases)

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def fixture(name):return json.loads(gzip.decompress((ROOT/'tests/fixtures'/f'application-console-{name}.json.gz').read_bytes()))

def validate(binary):
    registry=run(binary,[dict(op='registry')])[0];expected=registry_tree(fixture('registry-original'))
    if not registry['registered']or not registry['idempotent']or registry['effects']or sorted(registry['shells'],key=lambda s:s['name'])!=expected:raise RuntimeError('Console registry differs from original')
    cases=command_cases()
    if cases!=fixture('command-inputs'):raise RuntimeError('Changed case generator requires original replay')
    original=fixture('command-original');native=run(binary,cases)
    if len(cases)!=len(original):raise RuntimeError('Incomplete original command fixture')
    mismatches=[]
    for case,o,n in zip(cases,original,native):
        if not n['registered']or n['result']!=0 or project(case,n)!=o:mismatches.append(case['id'])
    if mismatches:raise RuntimeError('Console command mismatches: '+', '.join(mismatches))
    software_cases=[dict(direct_shell=s,direct_token=t,direct_argument='',missing_platform=True)for s,t in [('Runtime','t'),('PicoStill','test'),('PicoStill','t b')]]
    software_original=json.loads(gzip.decompress((ROOT/'tests/fixtures/software-console-original.json.gz').read_bytes()))['cases']
    for actual,index in zip(run(binary,software_cases),[0,10,12]):
        software_expected=software_original[index];effects=[]
        for event in actual['effects']:
            if event[0]=='scope':event[1]=int(event[1])
            effects.append('|'.join(str(value)for value in event))
        if not actual['registered']or actual['result']!=0 or effects!=software_expected['events']or [actual['state']['counter'],actual['state']['clock']]!=software_expected['state']:raise RuntimeError('Software diagnostic registry binding differs')
    backend_cases=[dict(direct_shell=s,direct_token='tests',direct_argument='',missing_platform=True)for s in ['HttpBackend','ZBackendT']]
    backend_original=json.loads(gzip.decompress((ROOT/'tests/fixtures/backend-console-original.json.gz').read_bytes()))['cases']
    for index,actual in enumerate(run(binary,backend_cases)):
        backend_expected=guarded_original(dict(operation=index),backend_original[index]);effects=[]
        for event in actual['effects']:
            if event[0]=='scope':event[1]=int(event[1])
            effects.append('|'.join(str(value)for value in event))
        if not actual['registered']or actual['result']!=0 or effects!=backend_expected['events']or [actual['state']['counter']-7,actual['state']['clock']]!=backend_expected['state']:raise RuntimeError('Backend diagnostic registry binding differs')
    basic_dispatch=validate_basic_dispatch(binary)
    network_dispatch=validate_network_dispatch(binary)
    paths=['include/zseries/application_console.hpp' ,'src/application_console.cpp','src/application_console_manifest.inc','src/application_console_information.inc','src/application_console_msp.inc','tests/application_console_probe.cpp','tests/application_console_cases.py','tests/application_console_native.py','tests/test_application_console.py','tools/validate_application_console.py','tools/ghidra/EmulateApplicationConsole.java','tools/ghidra/EmulateApplicationCommands.java','tools/ghidra/Z119Execution.java','analysis/application-console-callbacks.json','tests/fixtures/software-console-original.json.gz']
    paths +=['tests/fixtures/backend-console-original.json.gz','tools/validate_backend_console.py','tests/fixtures/basic-console-original.json.gz']
    paths +=['tests/fixtures/network-console-original.json.gz','tests/network_console_cases.py']
    paths +=[str(p.relative_to(ROOT))for p in sorted((ROOT/'tests/fixtures').glob('application-console-*.json.gz'))]
    registry_execution=json.loads((ROOT/'analysis/application-console-registry-execution.json').read_text())
    command_execution=json.loads((ROOT/'analysis/application-console-command-execution.json').read_text())
    callbacks=json.loads((ROOT/'analysis/application-console-callbacks.json').read_text())['callbacks']
    return dict(schema_version=1,input_sha256=command_execution['input_sha256'],all_results_match=True,
        software_diagnostic_dispatch_cases=3,backend_diagnostic_dispatch_cases=2,basic_diagnostic_dispatch_cases=basic_dispatch,network_integration_all_dispatch_cases=network_dispatch,registry_cases=registry_execution['cases'],shells=len(expected),commands=len(callbacks),child_links=sum('child'in e for s in expected for e in s['entries']),
        command_cases=len(cases),command_cases_by_kind=dict(collections.Counter(c['kind']for c in cases)),returning_assertions=sum(o['counter']-7 for o in original),
        registry_functions=len(registry_execution['original_functions_executed']),command_functions=len(command_execution['original_functions_executed']),
        callback_owners=dict(collections.Counter(c['owner']for c in callbacks)),source_sha256={p:digest(ROOT/p)for p in paths},
        scope='Actual OEM registry bodies and selected console callbacks/helpers execute through return, including the real assertion helper. Generic Shell insertion, Runtime/PicoStill mailboxes, and MSP command transmission are explicit typed boundaries. Native replay uses actual composed Core/controller methods. Runtime temperature comparison uses exact promoted IEEE-754 bits. Registry equality does not establish completion of all callback bodies.',
        pending_diagnostic_self_test_callbacks=[c['callback']for c in callbacks if c['owner']=='pending_diagnostic_self_test'],
        existing_data_only_self_test_callbacks=[],
        hardware_tested=False,live_network_tested=False,complete_application_linked=False)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path);parser.add_argument('--write-report',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='application-console-validation-')as directory:report=validate(args.binary or build(directory))
    if args.write_report:(ROOT/'analysis/application-console-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k]for k in ('all_results_match','registry_cases','shells','commands','child_links','command_cases','returning_assertions','callback_owners')},indent=2))
if __name__=='__main__':main()
