#!/usr/bin/env python3
"""Compare complete EEPROM and ADC diagnostic tests with original executions."""
from pathlib import Path
import argparse,gzip,hashlib,json,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from hardware_console_cases import cases
from hardware_console_native import load_native,run

def verify(directory):
 inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/hardware-console-inputs.json.gz').read_bytes()))
 original=json.loads(gzip.decompress((ROOT/'tests/fixtures/hardware-console-original.json.gz').read_bytes()))
 if inputs!=cases()or len(inputs)!=len(original['cases']):raise ValueError('Original inputs/count differ')
 lib=load_native(directory)
 for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
  if run(lib,case)!=expected:raise ValueError(f'Original hardware diagnostic differs in case {index}')
 paths=['include/zseries/eeprom.hpp','src/eeprom.cpp','include/zseries/hardware_console_tests.hpp','src/hardware_console_tests.cpp','tests/hardware_console_bridge.cpp','tests/hardware_console_cases.py','tests/hardware_console_native.py','tests/test_hardware_console.py','tests/ghidra/EmulateHardwareConsoleTests.java','tests/ghidra/SlideshowExecution.java','tools/validate_hardware_console.py','tests/fixtures/hardware-console-inputs.json.gz','tests/fixtures/hardware-console-original.json.gz']
 return dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(inputs),original_functions=len(original['original_functions']),original_thermistor_calls=original['original_functions']['0x400d6ac0'],returning_assertions=original['original_functions']['0x400db014'],all_results_match=True,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in paths},scope='Original EEPROM diagnostic 400dd2e0 and MAX11613 diagnostic 400ddee0 execute through return, including their full data/thermistor callees and ScopeLogger constructor/destructor. Every diagnostic event, final assertion counter and wrapping clock is compared. OEM assertion helper 400db014 executes, updates its counter and delays 200 ms before returning. ROM primitives, prescribed clock/stack high-water, soft float, finite positive log, formatted diagnostic capture, SDK task watchdog reset and scope text rendering are explicit environmental boundaries. No EEPROM flash or ADC bus access is performed by these original diagnostic bodies.',hardware_tested=False,complete_application_linked=False)

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--write-report',action='store_true');args=parser.parse_args()
 with tempfile.TemporaryDirectory(prefix='hardware-console-')as directory:report=verify(directory)
 if args.write_report:(ROOT/'analysis/hardware-console-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k]for k in ('cases','original_functions','original_thermistor_calls','returning_assertions','all_results_match')},indent=2))
if __name__=='__main__':main()
