#!/usr/bin/env python3
"""Replay original Pinger policy, assertion continuation and SDK call order."""
import argparse,ctypes,gzip,hashlib,io,itertools,json,os,shlex,subprocess,tempfile
from pathlib import Path
from validate_display import IMAGE_SHA,digest
ROOT=Path(__file__).resolve().parents[1]
OWNED=['include/zseries/pinger.hpp','src/pinger.cpp','platform/esp32/pinger_platform.hpp','platform/esp32/pinger_platform.cpp','tests/pinger_bridge.cpp','tools/validate_pinger.py','tools/ghidra/EmulatePinger.java','tools/ghidra/Z119Execution.java']
def input_cases():
    rows=[]
    def add(op=0,target=0x04040808,left=0,wifi=3,error=0,dns=1,resolved=0x04030201,parsed=0x04040808,text=0,stats=None):
        words=[op,target,left,wifi,error,dns,resolved,parsed,text,17,0,0,0x09080706,0,0]+(stats or [7,1,10,9,0,32,288,65,3,17])
        rows.append(dict(id=f'pinger-{len(rows)}',words=[x&0xffffffff for x in words]))
    for target,left,wifi,error in itertools.product([0,1,0x04040808,0xffffffff],[-1,0,1,11],[0,1,3,4,255],[-1,0,0x6002]):add(target=target,left=left,wifi=wifi,error=error)
    for op,text in itertools.product([1,2],[1,2]):add(op=op,text=text)
    for dns,address,left,wifi in itertools.product([0,1,2],[0,0x04030201,0xffffffff],[0,1],[0,3]):add(op=1,dns=dns,resolved=address,left=left,wifi=wifi)
    for address,left,wifi in itertools.product([0,0x04030201,0xffffffff],[-1,0,1],[0,3]):add(op=2,parsed=address,left=left,wifi=wifi)
    for left,received,total in itertools.product([-2147483648,-1,0,1,11],[0,1,3,10,0xffffffff],[0,65,0xffffffff]):
        add(op=3,left=left,stats=[7,1,10,received,2,32,320,total,3,17])
    add(op=4)
    return rows

def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));path=Path(directory)/'pinger.so'
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I',str(ROOT/'include'),str(ROOT/'src/pinger.cpp'),str(ROOT/'tests/pinger_bridge.cpp'),'-o',str(path)],check=True)
    native=ctypes.CDLL(str(path));native.pinger_case.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)];native.pinger_case.restype=ctypes.c_uint
    return native

def native_case(native,case):
    words=(ctypes.c_uint32*25)(*case['words']);effects=(ctypes.c_uint32*256)();n=native.pinger_case(words,effects)
    return dict(id=case['id'],words=list(words),effects=list(effects)[:n])

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path);parser.add_argument('--original-jsonl',type=Path);parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    cases=input_cases();data=(json.dumps(cases,separators=(',',':'))+'\n').encode()
    if args.prepare_input:args.prepare_input.write_bytes(data);print(f'Prepared {len(cases)} Pinger cases')
    if not args.original_jsonl:return
    report=json.loads(Path(str(args.original_jsonl)+'.json').read_text());original=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    assert report['input_sha256']==IMAGE_SHA==digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')
    assert report['input_file_sha256']==hashlib.sha256(data).hexdigest() and report['cases']==len(cases)==len(original)
    with tempfile.TemporaryDirectory() as folder:
        native=load_native(folder)
        for case,expected in zip(cases,original):
            actual=native_case(native,case)
            if actual!=expected:raise AssertionError(f"{case['id']}: {actual} != {expected}")
    if args.write_evidence:
        raw=io.BytesIO()
        with gzip.GzipFile(filename='',fileobj=raw,mode='wb',mtime=0) as f:f.write((json.dumps(dict(input_sha256=IMAGE_SHA,cases=[dict(input=a,original=b) for a,b in zip(cases,original)]),separators=(',',':'))+'\n').encode())
        fixture=ROOT/'tests/fixtures/pinger-original.json.gz';fixture.write_bytes(raw.getvalue())
        report.update(all_results_match=True,golden_fixture_sha256=digest(fixture),source_sha256={name:digest(ROOT/name) for name in OWNED})
        (ROOT/'analysis/pinger-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'All {len(cases)} original Pinger executions match')
if __name__=='__main__':main()
