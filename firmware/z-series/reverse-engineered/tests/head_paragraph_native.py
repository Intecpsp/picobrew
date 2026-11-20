import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def load_native(directory):
    library=Path(directory)/'head-paragraph.so'
    sources=['src/head_paragraph.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/display_effects.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_paragraph_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32)
    native.head_paragraph_test.argtypes=[words,ctypes.POINTER(ctypes.c_int32),ctypes.c_char_p,ctypes.POINTER(ctypes.c_char_p),ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint8),words];native.head_paragraph_test.restype=ctypes.c_uint
    return native

def run(native,case):
    args=(ctypes.c_uint32*5)(*[int(case[key])&0xffffffff for key in ('alignment','command','reserve','offset','carets')]);bounds=(ctypes.c_int32*4)(*case['rect'])
    lines=(ctypes.c_char_p*max(1,len(case['lines'])))(*[text.encode() for text in case['lines']]);pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*24)()
    status=native.head_paragraph_test(args,bounds,case['button'].encode(),lines,len(case['lines']),pixels,out)
    if status!=0:raise ValueError('Unexpected native status '+str(status)+' for '+str(case))
    return dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out))

def compared(original):return {key:original[key] for key in ('pixels_sha256','state')}
