#!/usr/bin/env python3
"""Replay complete original MSP recovery and self-test behavior."""
from pathlib import Path
import argparse,gzip,hashlib,json,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from msp_continuation_cases import cases
from msp_continuation_native import load_native,run

def verify(directory):
 inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/msp-continuation-inputs.json.gz').read_bytes()))
 original=json.loads(gzip.decompress((ROOT/'tests/fixtures/msp-continuation-original.json.gz').read_bytes()))
 if inputs!=cases()or len(inputs)!=len(original['cases']):raise ValueError('Original inputs/count differ')
 native=load_native(directory)
 for case,expected in zip(inputs,original['cases']):
  if run(native,case)!=expected:raise ValueError('Original MSP continuation differs: '+case['name'])
 paths=['include/zseries/msp_protocol.h','src/msp_protocol.c','include/zseries/msp_console_tests.hpp','src/msp_console_tests.cpp','src/msp_images.cpp','tests/msp_continuation_bridge.cpp','tests/msp_continuation_cases.py','tests/msp_continuation_native.py','tests/test_msp_continuation.py','tests/test_msp_protocol.py','tests/ghidra/EmulateMspContinuation.java','tests/ghidra/MspContinuationExecution.java','tools/validate_msp_continuation.py','tests/fixtures/msp-continuation-inputs.json.gz','tests/fixtures/msp-continuation-original.json.gz']
 return dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(inputs),original_functions=len(original['original_functions']),returning_assertions=original['original_functions']['0x400db014'],original_crc_byte_executions=original['original_functions']['0x400d2000'],all_results_match=True,all_response_queues_consumed=all(r['remaining']==0 for r in original['cases']),event_records=sum(len(r['events'])for r in original['cases']),validated_image_crc=original['validated_image_crc'],source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in paths},scope='Original MSP application/BSL transport, response handling, setup/update recovery, and every named MSP self-test execute through return. All bus frames, consumed response bytes, diagnostic order, state bytes, counters, wrapping clock and controller return codes match. The actual returning assertion helper and both debug/test ScopeLogger bodies execute. Original CRC8 LOOP instructions execute; the full image CRC is independently executed for unchanged and deliberately corrupted fixture data, then cached for repeated setup. SDK TwoWire, prescribed clock/stack high-water, task watchdog reset, ROM primitives, soft-float and scope text rendering are explicit environmental boundaries. No physical controller is contacted.',hardware_tested=False,complete_application_linked=False)

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--write-report',action='store_true');args=parser.parse_args()
 with tempfile.TemporaryDirectory(prefix='msp-continuation-')as directory:report=verify(directory)
 if args.write_report:(ROOT/'analysis/msp-continuation-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k]for k in ('cases','original_functions','returning_assertions','event_records','all_results_match','all_response_queues_consumed')},indent=2))
if __name__=='__main__':main()
