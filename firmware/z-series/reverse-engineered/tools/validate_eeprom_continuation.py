#!/usr/bin/env python3
from pathlib import Path
import argparse,gzip,hashlib,json,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from eeprom_continuation_cases import cases
from eeprom_continuation_native import load_native,run
def verify(directory):
 inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/eeprom-continuation-inputs.json.gz').read_bytes()))
 original=json.loads(gzip.decompress((ROOT/'tests/fixtures/eeprom-continuation-original.json.gz').read_bytes()))
 if inputs!=cases()or len(inputs)!=len(original['cases']):raise ValueError('Original inputs/count differ')
 lib=load_native(directory)
 for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
  if run(lib,case)!=expected:raise ValueError(f'Original EEPROM continuation differs in case {index}')
 paths=['include/zseries/eeprom.hpp','src/eeprom.cpp','tests/eeprom_cases.py','tests/eeprom_continuation_cases.py','tests/eeprom_continuation_bridge.cpp','tests/eeprom_continuation_native.py','tests/test_eeprom_continuation.py','tests/ghidra/EmulateEepromContinuation.java','tests/ghidra/SlideshowExecution.java','tools/validate_eeprom_continuation.py','tests/fixtures/eeprom-continuation-inputs.json.gz','tests/fixtures/eeprom-continuation-original.json.gz']
 return dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(inputs),original_functions=len(original['original_functions']),returning_assertions=original['original_functions']['0x400db014'],all_results_match=True,event_records=sum(len(r['events'])for r in original['cases']),source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in paths},scope='All original EEPROM data/lifecycle cases execute through return, including real returning assertions, report/normalization logs and subsequent get/read/write/reset/commit/registration behavior. Data/cache bytes, dirty/initialized state, assertion site/counter, wrapping clock and ordered diagnostics/storage events match. Original mapped bytes are verified before execution; only ROM memory/string, SDK begin/commit/cache sizing, console registration, diagnostic text capture and clock/delay/soft-float services are boundaries.',hardware_tested=False,complete_application_linked=False)
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--write-report',action='store_true');args=parser.parse_args()
 with tempfile.TemporaryDirectory(prefix='eeprom-continuation-')as directory:report=verify(directory)
 if args.write_report:(ROOT/'analysis/eeprom-continuation-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k]for k in ('cases','original_functions','returning_assertions','event_records','all_results_match')},indent=2))
if __name__=='__main__':main()
