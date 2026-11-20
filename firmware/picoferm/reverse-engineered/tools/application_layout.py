#!/usr/bin/env python3
"""Seed OEM callback/vtable/static-init entries missed by automatic analysis."""
import argparse,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def layout():
    result=json.loads((ROOT/'analysis/ghidra-layout.json').read_text())
    labels=[(0x40203014,'oem_ledsStateChanged','Constructor callback pointer at application+0x6e8'),
            (0x402039cc,'oem_calibration','Application vtable 0x401070e4'),
            (0x40208004,'oem_application_init','Static initializer preceding address0x40208020')]
    result['entry_points']=sorted(set(result['entry_points']+[item[0]for item in labels]))
    result['labels']=[dict(address=address,name=name,function=True,evidence=evidence,confidence='instruction-entry from OEM callback/vtable/init-array')for address,name,evidence in labels]
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args();path=ROOT/'analysis/application-ghidra-layout.json';value=json.dumps(layout(),indent=2)+'\n'
    if args.check:
        if path.read_text()!=value:raise SystemExit('Application Ghidra layout changed')
    else:path.write_text(value)
