#!/usr/bin/env python3
from pathlib import Path
import argparse,gzip,hashlib,json,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from head_slideshow_cases import cases
from head_slideshow_native import load_native,run

def verify(directory):
 inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-slideshow-inputs.json.gz').read_bytes()));original=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-slideshow-original.json.gz').read_bytes()))
 if inputs!=cases()or len(inputs)!=len(original['cases']):raise ValueError('Original inputs/count differ')
 lib=load_native(directory)
 for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
  if run(lib,case)!=expected:raise ValueError(f'Original slideshow comparison differs in case {index}')
 paths=['include/zseries/head_slideshow.hpp','src/head_slideshow.cpp','include/zseries/head_presentation.hpp','src/head_presentation.cpp','tests/head_slideshow_bridge.cpp','tests/head_slideshow_cases.py','tests/head_slideshow_native.py','tests/test_head_slideshow.py','tests/ghidra/EmulateHeadSlideshow.java','tests/ghidra/SlideshowExecution.java','tools/validate_head_slideshow.py','tests/fixtures/head-slideshow-inputs.json.gz','tests/fixtures/head-slideshow-original.json.gz']
 return dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(inputs),original_functions=len(original['original_functions']),all_results_match=True,all_framebuffers_match=True,returning_assertions=sum(c['state'][2]-7 for c in original['cases']),source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in paths},scope='Original Slideshow page renderers, entry constructors, population/selection, timers, reset and StabilityText self-test execute through return. All 4096 framebuffer bytes, dirty/assertion/counter/clock, entry timer/counter/title and diagnostic/scope callback sequence match. Root constructor geometry and initialization caller ordering, reviewed ROM/libc/soft-float helpers, clock/delay, stack high-water mark, task watchdog reset, scope renderer and HeadScreens reset are explicit boundaries. The original assertion body executes its counter update and delay; draw callees are never substituted.',complete_application_linked=False,hardware_tested=False)

def main():
 p=argparse.ArgumentParser();p.add_argument('--write-report',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='head-slideshow-')as directory:report=verify(directory)
 if a.write_report:(ROOT/'analysis/head-slideshow-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k]for k in ('cases','original_functions','all_results_match','all_framebuffers_match','returning_assertions')},indent=2))
if __name__=='__main__':main()
