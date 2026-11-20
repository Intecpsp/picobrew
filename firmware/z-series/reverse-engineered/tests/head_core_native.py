"""Compile and compare typed Head Core consumers against raw original events."""
import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'head-core.so'
    sources=['src/head_core_events.cpp','src/enums.cpp','src/ui_catalog.cpp','tests/head_core_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));pointer=ctypes.POINTER(ctypes.c_uint32)
    native.head_core_test.argtypes=[pointer,pointer,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,pointer,pointer];native.head_core_test.restype=ctypes.c_uint
    return native

def run(native,case):
    args=(ctypes.c_uint32*4)(*[case[key] for key in ('operation','language','pending','debug')]);state=(ctypes.c_uint32*16)(*[v&0xffffffff for v in case['state']]);out=(ctypes.c_uint32*11)();effects=(ctypes.c_uint32*16384)()
    status=native.head_core_test(args,state,bytes.fromhex(case['event']),case['token'].encode(),case['network'].encode(),out,effects)
    if status!=0:raise ValueError('Unexpected native status '+str(status))
    rows=[];i=0
    while i<out[10]:
        row=dict(zip(('kind','first','second','third'),effects[i:i+4]));i+=4
        for key in ('title','text','event'):
            size=effects[i];i+=1;row[key]=list(effects[i:i+size]);i+=size
        row['event']=hashlib.sha256(bytes(row['event'])).hexdigest() if row['event'] else ''
        rows.append(row)
    return dict(state=list(out[:5]),assertion_line=out[5],assertion_counter=out[6],event_pending=out[7],still=list(out[8:10]),effects=rows)

def compared(original):return {key:original[key] for key in ('state','assertion_line','assertion_counter','event_pending','still','effects')}
