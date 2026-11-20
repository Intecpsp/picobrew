#!/usr/bin/env python3
"""Compare complete original Display callbacks and diagnostic demonstrations."""
import argparse,ctypes,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import run_msp_emulation as common
CALLBACKS=[0x400d7020,0x400d1bfc,0x400d1b4c,0x400d1b5c,0x400e780c,0x400d0908,0x400d0920,0x400fce34,0x400e78b4,0x400e78c8,0x400e78dc,0x400e78f0,0x400e7db4,0x400e8510,0x400e85cc,0x400e8994,0x400e95b0,0x400ea520,0x400eaf50,0x400d09a4,0x400e7fbc,0x400f1318]
SOURCES=['include/zseries/display_console.hpp','src/display_console.cpp','include/zseries/display.hpp','src/display.cpp','include/zseries/display_effects.hpp','src/display_effects.cpp','include/zseries/display_text.hpp','src/display_text.cpp','include/zseries/display_fonts.hpp','src/display_fonts.cpp','include/zseries/ui_catalog.hpp','src/ui_catalog.cpp','tests/display_console_bridge.cpp','tests/test_display_console.py','tools/validate_display_console.py']
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def cases():
 out=[]
 def add(callback,arg='42',x=0,y=0,dirty=0,toggle=0,font=4,spacing=0):
  out.append(dict(id=f'{callback:08x}-{len(out)}',words=[callback,x&0xffffffff,y&0xffffffff,toggle,dirty,font,spacing]+[0]*12,argument=arg))
 for callback in CALLBACKS:add(callback,'Hello Pico' if callback==0x400e7db4 else '42')
 for x,y in [(-65,-33),(-31,7),(1,1),(64,32),(100,52),(128,64)]:add(0x400e780c,x=x,y=y)
 for font in range(5):
  for dirty in [0,1]:
   add(0x400ee634,'Network',font=font,spacing=3,dirty=dirty)
   add(0x400ee634,'Test failed: 17 assertions',font=font,spacing=1,dirty=dirty)
 for value in ['',None,'A','abcdefghijklmnopqrstuvwxyz0123456789','\u00b0C \u00b0F']:
  for dirty in [0,1]:add(0x400ee634,value,font=2,spacing=4,dirty=dirty)
 for callback in [0x400d0908,0x400d0920]:
  for arg in [' -17suffix','0','2147483647','-2147483648']:add(callback,arg)
 for font in range(5):
  add(0x400e7db4,'A long centered console string',font=font,spacing=1)
  add(0x400e7fbc,font=font,spacing=1)
 add(0x400e7db4,'')
 add(0x400d09a4,toggle=1)
 add(0x400d1bfc,dirty=1)
 return out
def verify_original(report,inputs):
 raw=common.INPUT.read_bytes()
 if report['input_sha256']!=hashlib.sha256(raw).hexdigest() or report['input_sha256']!=common.images.IMAGES[common.INPUT.name]:
  raise ValueError('Preserved original Display image differs')
 encoded=(json.dumps(inputs,indent=2)+'\n').encode()
 if report['input_file_sha256']!=hashlib.sha256(encoded).hexdigest() or report['cases']!=len(inputs):
  raise ValueError('Original execution input or case count differs')
 parsed=common.images.parse_image(raw)
 segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in parsed['segments']]
 for function in report['original_functions']:
  for span in function['ranges']:
   actual=hashlib.sha256(common.read_mapped(raw,segments,span['address'],span['size'])).hexdigest()
   if actual!=span['sha256']:raise ValueError('Original function bytes differ: '+function['address'])
 addresses={int(f['address'],16)for f in report['original_functions']}
 if not set(CALLBACKS+[0x400db014,0x400ea5a4,0x400ee634]).issubset(addresses):
  raise ValueError('Original callback or returning assertion coverage is incomplete')
def load_native(directory):
 path=Path(directory)/'display-console.so'
 source=[p for p in SOURCES if p.endswith('.cpp')]
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p)for p in source]+['-o',str(path)],check=True)
 lib=ctypes.CDLL(str(path));lib.display_console_case.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_char_p]+[ctypes.POINTER(ctypes.c_uint8)]*3+[ctypes.c_uint];lib.display_console_case.restype=ctypes.c_uint;return lib
def native_case(lib,case):
 words=(ctypes.c_uint32*19)(*case['words']);pixels=(ctypes.c_uint8*4096)();transfer=(ctypes.c_uint8*4096)();events=(ctypes.c_uint8*(5*1024*1024))()
 argument=None if case['argument'] is None else case['argument'].encode()
 n=lib.display_console_case(words,argument,pixels,transfer,events,len(events));data=bytes(events[:n]);frames=[];p=0
 def string():
  nonlocal p
  end=data.index(0,p);v=data[p:end];p=end+1;return v
 while p<len(data):
  tag=data[p];p+=1
  if tag==1:p+=2
  elif tag==2:p+=1
  elif tag==3:
   size=int.from_bytes(data[p:p+4],'little');p+=4;frames.append(hashlib.sha256(data[p:p+size]).hexdigest());p+=size
  elif tag==4:p+=4
  elif tag==5:p+=1;string();string();p+=4;string()
  elif tag==6:p+=1;string();string();p+=4;string()
  elif tag==7:string();p+=4;string();p+=4
  else:raise ValueError(('event',tag,p))
 return dict(id=case['id'],words=list(words),pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),transfer_sha256=hashlib.sha256(bytes(transfer)).hexdigest(),events_sha256=hashlib.sha256(data).hexdigest(),frames=frames)
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--inputs',type=Path);parser.add_argument('--original',type=Path);parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
 inputs=cases()
 if args.inputs:args.inputs.write_text(json.dumps(inputs,indent=2)+'\n');return
 if not args.original:parser.error('--original is required')
 report=json.loads(args.original.with_suffix(args.original.suffix+'.json').read_text())
 verify_original(report,inputs)
 original=[json.loads(line)for line in args.original.read_text().splitlines()]
 if len(original)!=len(inputs):raise ValueError('Original case count differs')
 with tempfile.TemporaryDirectory() as folder:
  lib=load_native(folder)
  for case,expected in zip(inputs,original):
   actual=native_case(lib,case)
   if actual!=expected:
    difference={key:dict(native=actual[key],original=expected[key])for key in actual if actual[key]!=expected[key]}
    if 'frames'in difference:
     a,b=actual['frames'],expected['frames'];difference['frames']=dict(native_count=len(a),original_count=len(b),first_different=next((i for i,(x,y)in enumerate(zip(a,b))if x!=y),None))
    raise ValueError(json.dumps(dict(case=case,difference=difference)))
 if args.write_evidence:
  fixture=ROOT/'tests/fixtures/display-console-original.json.gz'
  data=json.dumps(dict(cases=[dict(input=c,original=o)for c,o in zip(inputs,original)]),indent=2).encode()+b'\n';fixture.write_bytes(gzip.compress(data,mtime=0))
  report.update(all_results_match=True,source_sha256={p:digest(ROOT/p)for p in SOURCES},golden_fixture_sha256=digest(fixture),fixture_sha256=hashlib.sha256(data).hexdigest(),original_execution_sha256=digest(args.original),generator_sha256={p:digest(ROOT/p)for p in ['tools/ghidra/EmulateDisplayConsole.java','tools/ghidra/DisplayConsoleExecution.java']})
  (ROOT/'analysis/display-console-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(f'Verified {len(inputs)} complete original Display console/demo executions')
if __name__=='__main__':main()
