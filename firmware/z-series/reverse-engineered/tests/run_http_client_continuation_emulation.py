#!/usr/bin/env python3
"""Regenerate outgoing HTTP proof using the pinned original/tool references."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,tempfile
import run_msp_emulation as common
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_http_client_continuation as validation

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('project','ghidra-home','java-home','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():parser.error('Select a new output directory')
    if validation.digest(common.INPUT)!=validation.IMAGE_SHA:raise ValueError('Original image changed')
    evidence=common.shared.tool_evidence(args.ghidra_home.resolve(),args.java_home.resolve(),
        {'ghidra':args.ghidra_home.parent/common.shared.TOOL_PINS['ghidra']['filename'],
         'jdk':args.java_home.parent/common.shared.TOOL_PINS['jdk']['filename']},'Xtensa:LE:32:default')
    args.output.mkdir(parents=True);source=args.output/'inputs.json';source.write_bytes(validation.serialized_inputs())
    original=args.output/'original.jsonl'
    scripts=[ROOT/'tools/ghidra'/name for name in ('EmulateHttpClientContinuation.java','Z119Execution.java')]
    with tempfile.TemporaryDirectory(prefix='http-continuation-script-')as directory:
        for path in scripts:shutil.copyfile(path,Path(directory)/path.name)
        command=[str(args.ghidra_home/'support/analyzeHeadless'),str(args.project),'firmware','-process',common.INPUT.name,
            '-readOnly','-noanalysis','-max-cpu','1','-scriptPath',directory,'-postScript','EmulateHttpClientContinuation.java',str(source),str(original)]
        env=dict(os.environ,JAVA_HOME=str(args.java_home));env['PATH']=str(args.java_home/'bin')+os.pathsep+env.get('PATH','')
        with(args.output/'execution.log').open('w')as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1200)
    metadata=json.loads(Path(str(original)+'.json').read_text())
    if metadata['input_sha256']!=validation.IMAGE_SHA or metadata['cases']!=len(validation.input_cases()) or len(original.read_text().splitlines())!=metadata['cases']:raise ValueError('Incomplete original execution')
    (args.output/'execution-tools.json').write_text(json.dumps(dict(tools=evidence,script_sha256={p.name:validation.digest(p)for p in scripts},
        cases=metadata['cases'],project_read_only=True,analysis=False),indent=2)+'\n')
    print('Executed',metadata['cases'],'complete original outgoing HTTP cases')
if __name__=='__main__':main()
