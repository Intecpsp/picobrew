#!/usr/bin/env python3
"""Regenerate full PicoStill/controller execution using verified pinned tools."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,tempfile
import run_msp_emulation as common
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from validate_still_continuation import input_cases,MODULES

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 for name in ['project','ghidra-home','java-home','output']:parser.add_argument('--'+name,type=Path,required=True)
 args=parser.parse_args()
 if args.output.exists():parser.error('Select a new output directory')
 raw=common.INPUT.read_bytes();sha=hashlib.sha256(raw).hexdigest()
 if sha!=common.images.IMAGES[common.INPUT.name]:raise ValueError('Preserved image identity differs')
 parsed=common.images.parse_image(raw)
 evidence=common.shared.tool_evidence(args.ghidra_home.resolve(),args.java_home.resolve(),{'ghidra':args.ghidra_home.parent/common.shared.TOOL_PINS['ghidra']['filename'],'jdk':args.java_home.parent/common.shared.TOOL_PINS['jdk']['filename']},'Xtensa:LE:32:default')
 args.output.mkdir(parents=True)
 scripts=[ROOT/'tests/ghidra'/name for name in ['StillContinuationSupport.java','EmulatePicoStillContinuation.java','EmulateStillClientContinuation.java']]+[ROOT/'tools/ghidra/Z119Execution.java']
 with tempfile.TemporaryDirectory(prefix='still-continuation-')as directory:
  directory=Path(directory)
  for path in scripts:shutil.copyfile(path,directory/path.name)
  layout=directory/'layout.json';layout.write_text(json.dumps(dict(input_path=str(common.INPUT),segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in parsed['segments']])))
  command=[str(args.ghidra_home/'support/analyzeHeadless'),str(args.project),'firmware','-process',common.INPUT.name,'-readOnly','-noanalysis','-max-cpu','1','-scriptPath',str(directory)]
  for name,script in zip(MODULES,['EmulatePicoStillContinuation.java','EmulateStillClientContinuation.java']):
   source=args.output/(name+'-continuation-inputs.json');source.write_text(json.dumps(input_cases(name),separators=(',',':'))+'\n')
   command+=['-postScript',script,str(source),str(args.output/(name+'-continuation-original.jsonl')),str(layout)]
  env=dict(os.environ,JAVA_HOME=str(args.java_home));env['PATH']=str(args.java_home/'bin')+os.pathsep+env.get('PATH','')
  with(args.output/'execution.log').open('w')as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1200)
  for name in MODULES:
   output=args.output/(name+'-continuation-original.jsonl');metadata=json.loads(Path(str(output)+'.json').read_text())
   if metadata['input_sha256']!=sha or metadata['cases']!=len(input_cases(name))or len(output.read_text().splitlines())!=metadata['cases']:raise ValueError('Incomplete original output')
 (args.output/'execution-tools.json').write_text(json.dumps(dict(tools=evidence,script_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in scripts},cases=sum(len(input_cases(n))for n in MODULES),project_read_only=True,analysis=False,instruction_budget_per_function=20000),indent=2)+'\n')
 print('Wrote',sum(len(input_cases(n))for n in MODULES),'complete original PicoStill/controller cases')
if __name__=='__main__':main()
