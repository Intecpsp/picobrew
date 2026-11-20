#!/usr/bin/env python3
"""Compare full original Runtime continuations, assertions and IEEE-754 state."""
from pathlib import Path
import argparse,gzip,hashlib,json,struct,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import program_runtime_cases as inputs
from runtime_continuation_native import load_native,run

def exact(expected,actual):
 return expected==actual and all(struct.pack('<d',v)==struct.pack('<d',actual['doubles'][k])for k,v in expected['doubles'].items())
def validate(directory):
 specification=json.loads(gzip.decompress((ROOT/'tests/fixtures/runtime-continuation-inputs.json.gz').read_bytes()))
 original=json.loads(gzip.decompress((ROOT/'tests/fixtures/runtime-continuation-original.json.gz').read_bytes()))
 cases=inputs.cases()
 if specification!=dict(words=inputs.WORDS,doubles=inputs.DOUBLES,strings=inputs.STRINGS,cases=cases)or len(cases)!=len(original['cases']):raise ValueError('Original Runtime input specification changed')
 lib=load_native(directory)
 for index,(case,expected)in enumerate(zip(cases,original['cases'])):
  if not exact(expected,run(lib,case)):raise ValueError(f'Original Runtime continuation differs at case {index}: {case["group"]}')
 if lib.runtime_continuation_service_contracts()!=1023:raise ValueError('Runtime service failure contract differs')
 paths=['include/zseries/program_runtime.hpp','include/zseries/quantity.hpp','src/program_runtime.cpp','src/program_runtime_execution.cpp','src/quantity.cpp','src/runtime_instruction.cpp','tests/program_runtime_cases.py','tests/test_program_runtime.py','tests/runtime_continuation_native.py','tests/test_runtime_continuation.py','tests/ghidra/EmulateRuntimeContinuation.java','tests/run_runtime_continuation_emulation.py','tools/validate_runtime_continuation.py','tests/fixtures/runtime-continuation-inputs.json.gz','tests/fixtures/runtime-continuation-original.json.gz']
 return dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(cases),asserting_cases=sum(bool(c['diagnostics'])for c in original['cases']),returning_assertions=sum(len(c['diagnostics'])for c in original['cases']),diagnostic_logs=sum(len(c['logs'])for c in original['cases']),original_functions=len(original['original_functions']),binary64_values_compared=sum(len(c['doubles'])for c in original['cases']),all_results_match=True,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in paths},scope='All 5792 original Runtime input cases execute through their returns, including the 1023 previously stopped at diagnostic assertions. The actual assertion helper400db014 increments the counter and delays200ms, and actual unsigned conversion4012d42c executes. Complete projected state, exact binary64 state bits, expansion stack, names, return values, ordered device/queue effects, assertion text, wrapping clock/counter and Quantity/DropMonitor diagnostic text are compared. Reviewed register-window calls and sequential MEMW; other scalar math, clock/delay, diagnostic presentation/string operations, independently checked MSP sends and explicit heater/PicoStill/event services remain boundaries. This is not a full CPU, application-loop or physical-device execution.',hardware_tested=False,complete_application_linked=False)

def main():
 p=argparse.ArgumentParser();p.add_argument('--write-report',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='runtime-continuation-')as d:r=validate(d)
 if a.write_report:(ROOT/'analysis/runtime-continuation-validation.json').write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps({k:r[k]for k in ['cases','asserting_cases','returning_assertions','diagnostic_logs','original_functions','binary64_values_compared','all_results_match']},indent=2))
if __name__=='__main__':main()
