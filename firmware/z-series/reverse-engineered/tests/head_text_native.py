"""Native Head text-input source bridge."""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'head-text.so'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'src/head_text_input.cpp'),str(ROOT/'tests/head_text_bridge.cpp'),'-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));pointer=ctypes.POINTER(ctypes.c_uint32)
    native.head_text_test.argtypes=[ctypes.c_uint32,pointer,pointer,pointer,pointer];native.head_text_test.restype=ctypes.c_uint
    return native
def run(native,case):
    state=(ctypes.c_uint32*119)(*case['state']);text=(ctypes.c_uint32*65)(*case['text']);out=(ctypes.c_uint32*120)();effects=(ctypes.c_uint32*240)()
    status=native.head_text_test(case['command'],state,text,out,effects)
    if status:raise ValueError('Unexpected native status '+str(status))
    return dict(state=list(out[:119]),effects=[list(effects[i:i+120]) for i in range(0,out[119],120)])
def compared(original):return {key:original[key] for key in ('state','effects')}
