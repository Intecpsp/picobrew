#!/usr/bin/env python3
"""Replay the complete original HTTP bodies from a copied Ghidra project."""
import argparse,gzip,json,os,subprocess
from pathlib import Path
import http_cases
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--ghidra',type=Path,required=True);parser.add_argument('--jdk',type=Path,required=True);parser.add_argument('--project',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);inputs=args.output/'http-inputs.json.gz';original=args.output/'http-original.json.gz'
    inputs.write_bytes(gzip.compress((json.dumps(http_cases.cases(),separators=(',',':'))+'\n').encode(),mtime=0))
    environment=dict(os.environ,JAVA_HOME=str(args.jdk.resolve()))
    command=[str(args.ghidra/'support/analyzeHeadless'),str(args.project),'firmware','-process','picoferm_0_2_7.bin','-readOnly','-noanalysis','-scriptPath',str(ROOT/'tools/ghidra'),'-postScript','EmulatePicoFermHttp.java',str(inputs),str(original)]
    with (args.output/'headless.log').open('w') as log:subprocess.run(command,env=environment,stdout=log,stderr=subprocess.STDOUT,check=True)
    if not original.is_file():raise SystemExit('Original execution did not finish; see headless.log')
    data=json.loads(gzip.decompress(original.read_bytes()));original.write_bytes(gzip.compress((json.dumps(data,separators=(',',':'))+'\n').encode(),mtime=0))
    print('Executed',len(data['cases']),'complete original HTTP cases')
if __name__=='__main__':main()
