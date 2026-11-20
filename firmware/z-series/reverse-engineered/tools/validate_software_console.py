#!/usr/bin/env python3
"""Compare full Runtime/PicoStill diagnostic callbacks with original execution."""
from pathlib import Path
import argparse,gzip,hashlib,json,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from software_console_cases import cases
from software_console_native import load_native,run,SOURCES

def verify(directory):
 inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/software-console-inputs.json.gz').read_bytes()))
 original=json.loads(gzip.decompress((ROOT/'tests/fixtures/software-console-original.json.gz').read_bytes()))
 if inputs!=cases()or len(inputs)!=len(original['cases']):raise ValueError('Original inputs/count differ')
 lib=load_native(directory)
 for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
  if run(lib,case)!=expected:raise ValueError(f'Original software diagnostic differs in case {index}')
 paths=SOURCES+['include/zseries/software_console_tests.hpp','include/zseries/serial_logger.hpp','tests/software_console_cases.py','tests/software_console_native.py','tests/test_software_console.py','tests/ghidra/EmulateSoftwareConsoleTests.java','tests/ghidra/SoftwareConsoleExecution.java','tools/validate_software_console.py','tests/fixtures/software-console-inputs.json.gz','tests/fixtures/software-console-original.json.gz']
 return dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(inputs),original_functions=len(original['original_functions']),returning_assertions=original['original_functions']['0x400db014'],all_results_match=True,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in paths},scope='Complete original Runtime 400e0e70, PicoStill sensors400eea38, state400f610c, parser4010d824, authorization40119eb8 and aggregate4011a204/4011a1fc callbacks execute through return. Production instruction stack/program catalog/formatters, sample parser/statistics, concrete CSV and selected pinned ArduinoJson decoder/serializer run in the reconstruction. Every observable diagnostic and scope event, assertion counter and wrapping clock is compared. OEM assertion400db014 executes its actual counter update and 200ms delay then returns. All mapped image segments are byte-verified. ROM strings/memory, scalar math, bounded printf/atof, clock, stack high water, C++ guards, stdout and ScopeLogger text rendering are explicit environment boundaries. No peripheral transactions, live cloud requests or physical hardware are exercised. These self-test paths do not by themselves establish malformed production-input continuation parity.',hardware_tested=False,complete_application_linked=False)

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--write-report',action='store_true');args=parser.parse_args()
 with tempfile.TemporaryDirectory(prefix='software-console-')as directory:report=verify(directory)
 if args.write_report:(ROOT/'analysis/software-console-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k]for k in ('cases','original_functions','returning_assertions','all_results_match')},indent=2))
if __name__=='__main__':main()
