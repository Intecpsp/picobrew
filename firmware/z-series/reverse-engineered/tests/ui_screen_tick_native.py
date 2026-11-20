"""Native ScreenLibrary tick comparison bridge."""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'ui-screen-tick.so'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'src/ui_screen_tick.cpp'),str(ROOT/'tests/ui_screen_tick_bridge.cpp'),'-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));pointer=ctypes.POINTER(ctypes.c_uint32)
    native.ui_screen_tick_test.argtypes=[pointer,ctypes.c_uint32,pointer,pointer]
    native.ui_screen_tick_test.restype=ctypes.c_uint
    return native
def run(native,case):
    state=(ctypes.c_uint32*24)(*case['state']);out=(ctypes.c_uint32*25)();effects=(ctypes.c_uint32*48)()
    status=native.ui_screen_tick_test(state,case['now'],out,effects)
    if status:raise ValueError('Unexpected native status '+str(status))
    return dict(state=list(out[:24]),effects=[list(effects[i:i+3]) for i in range(0,out[24],3)])
def compared(original):return original
