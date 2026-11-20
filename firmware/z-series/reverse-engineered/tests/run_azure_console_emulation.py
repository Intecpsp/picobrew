#!/usr/bin/env python3
"""Run complete original Azure diagnostic with only hashed secret traces."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import run_msp_emulation as common
from azure_console_cases import cases

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('project','ghidra-home','java-home','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--limit',type=int)
    args=parser.parse_args()
    if args.output.exists():parser.error('Select a new output directory')
    import hashlib
    if hashlib.sha256(common.INPUT.read_bytes()).hexdigest()!='d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27':raise ValueError('OEM identity mismatch')
    evidence=common.shared.tool_evidence(args.ghidra_home.resolve(),args.java_home.resolve(),
        {'ghidra':args.ghidra_home.parent/common.shared.TOOL_PINS['ghidra']['filename'],
         'jdk':args.java_home.parent/common.shared.TOOL_PINS['jdk']['filename']},'Xtensa:LE:32:default')
    args.output.mkdir(parents=True);source=args.output/'inputs.json';source.write_text(json.dumps(cases()[:args.limit],separators=(',',':'))+'\n')
    original=args.output/'original.jsonl';scripts=[ROOT/'tools/ghidra'/name for name in ('EmulateAzureConsole.java','Z119Execution.java')]
    with tempfile.TemporaryDirectory(prefix='azure-console-script-')as directory:
        for path in scripts:shutil.copyfile(path,Path(directory)/path.name)
        command=[str(args.ghidra_home/'support/analyzeHeadless'),str(args.project),'firmware','-process',common.INPUT.name,
            '-readOnly','-noanalysis','-max-cpu','1','-scriptPath',directory,'-postScript','EmulateAzureConsole.java',str(source),str(original)]
        env=dict(os.environ,JAVA_HOME=str(args.java_home),JAVA_TOOL_OPTIONS='-XX:-UsePerfData');env['PATH']=str(args.java_home/'bin')+os.pathsep+env.get('PATH','')
        with(args.output/'execution.log').open('w')as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1200)
    metadata=json.loads(Path(str(original)+'.json').read_text())
    if metadata['input_sha256']!='d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27' or metadata['cases']!=len(cases()[:args.limit]):raise ValueError('Incomplete original execution')
    (args.output/'execution-tools.json').write_text(json.dumps(dict(tools=evidence,script_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in scripts},
        cases=metadata['cases'],project_read_only=True,analysis=False),indent=2)+'\n')
    print('Executed',metadata['cases'],'complete original Azure cases; private resources omitted')
if __name__=='__main__':main()
