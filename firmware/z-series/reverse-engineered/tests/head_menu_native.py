import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def load_native(directory):
    library=Path(directory)/'head-menu.so'
    sources=['src/head_menu.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/display_effects.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_menu_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);strings=ctypes.POINTER(ctypes.c_char_p);pixels=ctypes.POINTER(ctypes.c_uint8)
    native.head_menu_test.argtypes=[words,ctypes.POINTER(ctypes.c_int32),ctypes.c_char_p,strings,ctypes.c_uint32,strings,ctypes.c_uint32,words,ctypes.c_uint32,pixels,pixels,words];native.head_menu_test.restype=ctypes.c_uint
    return native

def run(native,case):
    args=(ctypes.c_uint32*8)(*[int(case[key])&0xffffffff for key in ('operation','rows','vertical','back','selected','body','horizontal','body_vertical')]);bounds=(ctypes.c_int32*4)(*case['rect'])
    labels=(ctypes.c_char_p*max(1,len(case['labels'])))(*[text.encode() for text in case['labels']]);lines=(ctypes.c_char_p*max(1,len(case['lines'])))(*[text.encode() for text in case['lines']]);ticks=(ctypes.c_uint32*max(1,len(case['ticks'])))(*case['ticks'])
    pixels=(ctypes.c_uint8*4096)();marquee=(ctypes.c_uint8*3840)();out=(ctypes.c_uint32*29)()
    status=native.head_menu_test(args,bounds,case['title'].encode(),labels,len(case['labels']),lines,len(case['lines']),ticks,len(case['ticks']),pixels,marquee,out)
    if status!=0:raise ValueError('Unexpected native status '+str(status)+' for '+str(case))
    return dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),marquee_sha256=hashlib.sha256(bytes(marquee)).hexdigest(),state=list(out))

def compared(original):return {key:original[key] for key in ('pixels_sha256','marquee_sha256','state')}
