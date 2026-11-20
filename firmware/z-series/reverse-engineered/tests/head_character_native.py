import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'head-character.so'
    sources=['src/head_character.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/display_effects.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_character_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32)
    native.head_character_test.argtypes=[words,ctypes.c_char_p,ctypes.POINTER(ctypes.c_uint8),words,ctypes.c_void_p,ctypes.c_void_p];native.head_character_test.restype=ctypes.c_uint
    return native
def run(native,case):
    values=[int(case[k])&0xffffffff for k in ['operation','kind','language','enabled','visible','selected','limit','command','truncated']]+case['rect'];args=(ctypes.c_uint32*len(values))(*values)
    pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*9)();text=ctypes.create_string_buffer(65);event=ctypes.create_string_buffer(65)
    status=native.head_character_test(args,case['text'].encode(),pixels,out,text,event)
    if status:raise ValueError(f'Unexpected character status {status}: {case}')
    return dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out)[:7],text=text.value.decode(),events=[] if not out[7] else [['cancel' if out[7]==1 else 'submit',out[8],event.value.decode()]])
def compared(original):return original
