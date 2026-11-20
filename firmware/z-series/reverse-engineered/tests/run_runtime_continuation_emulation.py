#!/usr/bin/env python3
"""Replay all Runtime cases through the original returning assertion helper."""
from pathlib import Path
import argparse,gzip,hashlib,json,os,shutil,subprocess,tempfile
import program_runtime_cases as inputs
import run_msp_emulation as common
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--ghidra-home',type=Path,required=True);p.add_argument('--java-home',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('Use a new output directory')
 raw=common.INPUT.read_bytes();sha=hashlib.sha256(raw).hexdigest()
 if sha!=common.images.IMAGES[common.INPUT.name]:raise ValueError('Preserved image identity differs')
 spec=dict(words=inputs.WORDS,doubles=inputs.DOUBLES,strings=inputs.STRINGS,cases=inputs.cases());parsed=common.images.parse_image(raw)
 evidence=common.shared.tool_evidence(a.ghidra_home.resolve(),a.java_home.resolve(),{'ghidra':a.ghidra_home.parent/common.shared.TOOL_PINS['ghidra']['filename'],'jdk':a.java_home.parent/common.shared.TOOL_PINS['jdk']['filename']},'Xtensa:LE:32:default')
 a.output.mkdir(parents=True)
 with tempfile.TemporaryDirectory(prefix='runtime-continuation-')as d:
  d=Path(d);script=ROOT/'tests/ghidra/EmulateRuntimeContinuation.java';shutil.copyfile(script,d/script.name)
  (d/'inputs.json').write_text(json.dumps(spec,separators=(',',':'))+'\n');(d/'layout.json').write_text(json.dumps(dict(input_path=str(common.INPUT),segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in parsed['segments']])))
  env=dict(os.environ,JAVA_HOME=str(a.java_home));env['PATH']=str(a.java_home/'bin')+os.pathsep+env.get('PATH','')
  with(a.output/'execution.log').open('w')as log:subprocess.run([str(a.ghidra_home/'support/analyzeHeadless'),str(a.project),'firmware','-process',common.INPUT.name,'-readOnly','-noanalysis','-max-cpu','1','-scriptPath',str(d),'-postScript',script.name,str(d/'inputs.json'),str(d/'layout.json'),str(d/'original.json')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1200)
  result=json.loads((d/'original.json').read_text())
  if result['input_sha256']!=sha or len(result['cases'])!=len(spec['cases']):raise ValueError('Incomplete original Runtime output')
  for name,value in [('runtime-continuation-inputs',spec),('runtime-continuation-original',result)]:
   (a.output/(name+'.json.gz')).write_bytes(gzip.compress((json.dumps(value,separators=(',',':'))+'\n').encode(),mtime=0))
 (a.output/'execution-tools.json').write_text(json.dumps(dict(tools=evidence,script_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),cases=len(spec['cases']),project_read_only=True,analysis=False,instruction_budget_per_case=500000),indent=2)+'\n')
 print('Wrote',len(spec['cases']),'full original Runtime cases')
if __name__=='__main__':main()
