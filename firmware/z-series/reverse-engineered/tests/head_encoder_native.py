"""Compile and call the recovered encoder selection body."""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def load_native(directory):
    library=Path(directory)/'head-encoder.so'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'src/head_encoder.cpp'),str(ROOT/'src/head_flow.cpp'),str(ROOT/'tests/head_encoder_bridge.cpp'),'-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));pointer=ctypes.POINTER(ctypes.c_uint32)
    native.head_encoder_test.argtypes=[pointer,ctypes.c_uint32,ctypes.c_uint32,pointer,pointer]
    native.head_encoder_test.restype=ctypes.c_uint
    return native

def run(native,case):
    state=(ctypes.c_uint32*44)(*case['state']);out=(ctypes.c_uint32*46)();effects=(ctypes.c_uint32*96)()
    status=native.head_encoder_test(state,case['delta'],case['clock'],out,effects)
    if status not in (0,3,5):raise ValueError('Unexpected native status '+str(status))
    return dict(state=list(out[:44]),assertion_line=out[44],effects=[list(effects[i:i+3]) for i in range(0,out[45],3)])

def compared(original):return {key:original[key] for key in ('state','assertion_line','effects')}
