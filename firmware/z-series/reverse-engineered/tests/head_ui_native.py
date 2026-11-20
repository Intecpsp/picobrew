"""Native exact action/state capture for the recovered UI command body."""
import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'head-ui.so'
    sources=['src/head_ui_commands.cpp','src/enums.cpp','src/ui_catalog.cpp','tests/head_ui_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));pointer=ctypes.POINTER(ctypes.c_uint32)
    native.head_ui_test.argtypes=[pointer,pointer,pointer,pointer,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,pointer,pointer];native.head_ui_test.restype=ctypes.c_uint
    return native

def run(native,case):
    args=(ctypes.c_uint32*6)(*[case[key] for key in ('command','language','flags','index','pending','debug')]);state=(ctypes.c_uint32*17)(*[v&0xffffffff for v in case['state']]);parameters=(ctypes.c_uint32*32)(*[v&0xffffffff for v in case['parameters']]);manual=(ctypes.c_uint32*15)(*case['manual']);out=(ctypes.c_uint32*236)();effects=(ctypes.c_uint32*32768)()
    status=native.head_ui_test(args,state,parameters,manual,bytes.fromhex(case['selected']),bytes.fromhex(case['payload']),case['search'].encode(),case['name'].encode(),out,effects)
    if status!=0:raise ValueError('Unexpected native status '+str(status))
    rows=[];i=0
    while i<out[235]:
        row=dict(zip(('kind','first','second','third'),effects[i:i+4]));i+=4
        for key in ('title','text','screen','command','search','recipe','event'):
            size=effects[i];i+=1;row[key]=list(effects[i:i+size]);i+=size
        for key in ('recipe','event'):row[key]=hashlib.sha256(bytes(row[key])).hexdigest() if row[key] else ''
        rows.append(row)
    return dict(state=list(out[:11]),parameters=list(out[11:27]),manual=list(out[27:42]),search=list(out[42:59]),recipe=hashlib.sha256(bytes(out[59:235])).hexdigest(),effects=rows)

def compared(original):return {key:original[key] for key in ('state','parameters','manual','search','recipe','effects')}
