import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'head-widgets.so'
    sources=['src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_widgets_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));pointer=ctypes.POINTER(ctypes.c_uint32)
    native.head_widgets_test.argtypes=[pointer,ctypes.POINTER(ctypes.c_int32),ctypes.c_char_p,ctypes.c_char_p,ctypes.POINTER(ctypes.c_char_p),ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint8),pointer];native.head_widgets_test.restype=ctypes.c_uint
    return native

def run(native,case):
    args=(ctypes.c_uint32*10)(*[case[key]&0xffffffff for key in ('operation','value','flag','language','font','horizontal','vertical','padding','seed','counter')]);bounds=(ctypes.c_int32*4)(*case['rect']);lines=(ctypes.c_char_p*max(1,len(case['lines'])))(*[text.encode() for text in case['lines']]);pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*4)()
    status=native.head_widgets_test(args,bounds,case['first'].encode(),case['second'].encode(),lines,len(case['lines']),pixels,out)
    if status!=0:raise ValueError('Unexpected native status '+str(status))
    return dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out))

def compared(original):return {key:original[key] for key in ('pixels_sha256','state')}
