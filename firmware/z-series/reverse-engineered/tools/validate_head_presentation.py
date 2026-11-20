#!/usr/bin/env python3
from pathlib import Path
import gzip,hashlib,json,sys,tempfile,argparse
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from head_presentation_cases import cases
from head_presentation_native import load_native,run

def verify(directory):
 inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-presentation-inputs.json.gz').read_bytes()))
 original=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-presentation-original.json.gz').read_bytes()))
 if inputs!=cases()or len(inputs)!=len(original['cases']):raise ValueError('Original inputs/count differ')
 lib=load_native(directory)
 for index,(c,expected)in enumerate(zip(inputs,original['cases'])):
  if run(lib,c)!=expected:raise ValueError(f'Original framebuffer differs in case{index}')
 paths=['include/zseries/head_presentation.hpp','include/zseries/head_hardware.hpp','include/zseries/head_still_display.hpp','src/head_presentation.cpp','src/head_hardware.cpp','src/head_still_display.cpp','tests/head_presentation_bridge.cpp','tests/head_presentation_cases.py','tests/head_presentation_native.py','tests/test_head_presentation.py','tests/ghidra/EmulateHeadPresentation.java','tools/ghidra/DisplayExecution.java','tools/validate_head_presentation.py','tests/fixtures/head-presentation-inputs.json.gz','tests/fixtures/head-presentation-original.json.gz']
 return dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(inputs),original_functions=len(original['original_functions']),all_framebuffers_match=True,all_state_results_match=True,returning_assertions=sum(r['state'][2]-7 for r in original['cases']),source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in paths},scope='Original Hardware/T1 monitor400f0ab4 and Still transient400f1774 plus their Graphics/TextLines/display/UTF8/font/bitmap callees execute through return. Constructor fields, ROM primitives, fixed-point snprintf/vsnprintf, soft float, clock/log/delay and Xtensa windowed ABI are explicit fixture boundaries. All4096 framebuffer bytes, dirty/last assertion/counter/clock are compared.',hardware_tested=False,complete_application_linked=False)
def main():
 p=argparse.ArgumentParser();p.add_argument('--write-report',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='head-presentation-')as directory:r=verify(directory)
 if a.write_report:(ROOT/'analysis/head-presentation-validation.json').write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps({k:r[k]for k in ('cases','original_functions','all_framebuffers_match','all_state_results_match','returning_assertions')},indent=2))
if __name__=='__main__':main()
