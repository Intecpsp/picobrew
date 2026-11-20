#!/usr/bin/env python3
"""Compare complete PicoStill controller/client execution and returning diagnostics."""
from pathlib import Path
import argparse,ctypes,gzip,hashlib,json,os,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_pico_still as controller
import validate_still_client as client
MODULES={'pico-still':controller,'still-client':client}
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def input_cases(name):
 module=MODULES[name];rows=module.input_cases()
 # Exercise clock and assertion counter wrapping independently of state timers.
 chosen=([controller.case('continuation-retry-wrap','retry',retry=-2,maximum=-1,argument=3),
          controller.case('continuation-unhandled-wrap',state=0,wifi_pending=1,wifi=7),
          controller.case('continuation-pending-wrap',state=10,pending=0,error=1),
          controller.case('continuation-empty-ip-wrap','transition',argument=12,ip=2)] if name=='pico-still' else
         [client.case('continuation-zero-capacity-wrap','encode',capacity=0),
          client.case('continuation-empty-command-wrap',pending=0,text=''),
          client.case('continuation-bad-data-wrap',pending=9,text='#bad#'),
          client.case('continuation-empty-fields-wrap','parse',text='#,,,,,,,,None#'),
          client.case('continuation-empty-host-wrap','host',text=''),
          client.case('continuation-long-token-wrap','parse',text='#1234567890123456,2,3,4,5,1,1,1,None#')])
 for i,row in enumerate(chosen):
  row['words'][2 if name=='pico-still' else 12]=0xffffff80
  row['counter']=0xfffffffe if i&1 else 0xffffffff
  rows.append(row)
 return rows

def load_native(name,directory):
 previous=os.environ.get('CXX');os.environ['CXX']=(previous or 'c++')+' -DSTILL_CONTINUATION_DIAGNOSTICS'
 try:lib,compiler=MODULES[name].load_native(directory)
 finally:
  if previous is None:os.environ.pop('CXX',None)
  else:os.environ['CXX']=previous
 lib.still_continuation_begin.argtypes=[ctypes.c_uint32,ctypes.c_uint32]
 lib.still_continuation_output.argtypes=[ctypes.POINTER(ctypes.c_char),ctypes.c_size_t,ctypes.POINTER(ctypes.c_uint32)]
 return lib,compiler

def native_case(name,lib,item):
 lib.still_continuation_begin(item['words'][2 if name=='pico-still' else 12],item.get('counter',0xffffffff))
 actual=MODULES[name].native_case(lib,item)
 buffer=ctypes.create_string_buffer(65536);state=(ctypes.c_uint32*2)();count=lib.still_continuation_output(buffer,len(buffer),state)
 if count<0:raise ValueError('Diagnostic trace capacity exceeded')
 actual.update(trace=buffer.raw[:count].decode('latin1').split('\0')[:-1],clock=state[0],counter=state[1])
 return actual

SOURCE_PATHS=['include/zseries/pico_still.hpp','src/pico_still.cpp','include/zseries/pico_still_sensors.hpp','src/pico_still_sensors.cpp',
 'include/zseries/still_client.hpp','src/still_client.cpp','include/zseries/still_diagnostics.hpp','include/zseries/console.hpp',
 'include/zseries/still_http.hpp','src/still_http.cpp','src/enums.cpp','src/eeprom.cpp','tests/pico_still_bridge.cpp','tests/still_client_bridge.cpp',
 'tests/still_continuation_probe.hpp','tests/still_continuation_contracts.cpp','tests/test_still_continuation.py','tools/validate_pico_still.py','tools/validate_still_client.py',
 'tools/validate_still_continuation.py','tools/ghidra/Z119Execution.java','tests/ghidra/StillContinuationSupport.java',
 'analysis/still-continuation-execution.json','tests/ghidra/EmulatePicoStillContinuation.java','tests/ghidra/EmulateStillClientContinuation.java','tests/run_still_continuation_emulation.py']

def verify(directory):
 fixture=ROOT/'tests/fixtures/still-continuation-original.json.gz'
 stored=json.loads(gzip.decompress(fixture.read_bytes()))
 if stored['input_sha256']!=controller.IMAGE_SHA or digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=controller.IMAGE_SHA:raise ValueError('Original image differs')
 report=dict(schema_version=1,input_sha256=controller.IMAGE_SHA,all_results_match=True,hardware_tested=False,complete_application_linked=False,groups={})
 total_functions=set();assertions=logs=0
 for name,module in MODULES.items():
  rows=input_cases(name);part=stored['groups'][name]
  if part['inputs']!=rows or len(rows)!=len(part['original']):raise ValueError('Inputs differ: '+name)
  lib,compiler=load_native(name,directory)
  for item,expected in zip(rows,part['original']):
   actual=native_case(name,lib,item)
   if actual!=expected:
    fields=[k for k in actual if actual[k]!=expected[k]]
    raise ValueError(f'{name} differs at {item["id"]}: {fields}')
  trace=[event for row in part['original']for event in row['trace']]
  counts=dict(cases=len(rows),returning_assertions=sum(x.startswith('assertion|')for x in trace),exact_logs=sum(x.startswith('log|')for x in trace))
  report['groups'][name]=dict(**counts,compiler=compiler,original_functions_executed=part['execution']['original_functions_executed'],modeled_boundaries=part['execution']['modeled_boundaries'])
  total_functions.update(part['execution']['original_functions_executed']);assertions+=counts['returning_assertions'];logs+=counts['exact_logs']
 report.update(cases=sum(g['cases']for g in report['groups'].values()),original_functions=len(total_functions),returning_assertions=assertions,exact_logs=logs,
  scope='Complete original PicoStill controller and StillClient/protocol matrices, including parser/statistics failure continuation, execute through return. Every retained state word, binary64 value, return, ordered service/diagnostic event, assertion counter and wrapping clock is compared. The original 400db014 helper executes its real counter increment and SDK 200ms delay. Both mapped image segments and input case hashes are verified. Network/client calls, scan strings, runtime ownership, event delivery, ROM memory/string operations, scalar arithmetic, finite decimal atof/hex snprintf and System rendering remain explicit boundaries. No live network, peripheral or physical hardware execution is claimed.',
  guarded_deviations=['Controller selection outside its eight entries is rejected before OEM invalid pointer arithmetic.',
   'A failed credential form encoding is rejected rather than using the OEM unterminated stack prefix.',
   'Public parser spans are bounded; the empty error-buffer under-read remains guarded after its returning assertion.',
   'A null nonempty body span reports the original assertion, leaves the buffer intact and returns the explicit InvalidBody API status.'],
  source_sha256={p:digest(ROOT/p)for p in SOURCE_PATHS},fixture_sha256=digest(fixture))
 return report

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input-dir',type=Path);parser.add_argument('--import-original-dir',type=Path);parser.add_argument('--write-report',action='store_true');args=parser.parse_args()
 if args.prepare_input_dir:
  args.prepare_input_dir.mkdir(parents=True,exist_ok=True)
  for name in MODULES:(args.prepare_input_dir/(name+'-continuation-inputs.json')).write_text(json.dumps(input_cases(name),separators=(',',':'))+'\n')
  if not args.import_original_dir and not args.write_report:return
 if args.import_original_dir:
  groups={}
  for name in MODULES:
   inputs=args.import_original_dir/(name+'-continuation-inputs.json');path=args.import_original_dir/(name+'-continuation-original.jsonl');metadata=json.loads(Path(str(path)+'.json').read_text())
   rows=input_cases(name);original=[json.loads(row)for row in path.read_text().splitlines()]
   if metadata['input_file_sha256']!=digest(inputs)or metadata['input_sha256']!=controller.IMAGE_SHA or metadata['cases']!=len(rows)or len(original)!=len(rows)or json.loads(inputs.read_text())!=rows:raise ValueError('Original input identity mismatch')
   if '0x400db014'not in metadata['original_functions_executed']:raise ValueError('Original assertion body did not run')
   groups[name]=dict(inputs=rows,original=original,execution=metadata)
  payload=json.dumps(dict(input_sha256=controller.IMAGE_SHA,groups=groups),separators=(',',':'))+'\n'
  (ROOT/'tests/fixtures/still-continuation-original.json.gz').write_bytes(gzip.compress(payload.encode(),mtime=0))
 with tempfile.TemporaryDirectory(prefix='still-continuation-')as directory:report=verify(directory)
 if args.write_report:(ROOT/'analysis/still-continuation-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k]for k in ['cases','original_functions','returning_assertions','exact_logs','all_results_match']},indent=2))
if __name__=='__main__':main()
