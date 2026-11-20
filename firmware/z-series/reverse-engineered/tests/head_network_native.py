"""Native Wi-Fi/Azure Head action and parameter bridge."""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'head-network.so'
    sources=['src/head_network_events.cpp','src/quantity.cpp','src/enums.cpp','src/ui_catalog.cpp','tests/head_network_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));pointer=ctypes.POINTER(ctypes.c_uint32)
    native.head_network_test.argtypes=[pointer,pointer,pointer,pointer,pointer];native.head_network_test.restype=ctypes.c_uint
    return native
def run(native,case):
    args=(ctypes.c_uint32*6)(*[case[key] for key in ('operation','core','language','event','argument','second')])
    state=(ctypes.c_uint32*16)(*case['state']);payload=(ctypes.c_uint32*16)(*case['payload']);out=(ctypes.c_uint32*18)();effects=(ctypes.c_uint32*2048)()
    status=native.head_network_test(args,state,payload,out,effects)
    if status not in (0,3):raise ValueError('Unexpected native status '+str(status))
    rows=[];i=0
    while i<out[17]:
        row=dict(zip(('kind','first','second','third'),effects[i:i+4]));i+=4
        for key in ('title','text'):
            size=effects[i];i+=1;row[key]=list(effects[i:i+size]);i+=size
        rows.append(row)
    return dict(state=list(out[:16]),assertion_line=out[16],effects=rows)
def compared(original):return {key:original[key] for key in ('state','assertion_line','effects')}
