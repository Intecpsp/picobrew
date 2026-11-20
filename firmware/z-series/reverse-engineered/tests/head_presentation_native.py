import ctypes,hashlib,os,shlex,subprocess
from pathlib import Path
from head_presentation_cases import WORDS,DOUBLES
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
 library=Path(directory)/'head-presentation.so'
 sources=['src/head_hardware.cpp','src/head_still_display.cpp','src/head_presentation.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_presentation_bridge.cpp']
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p)for p in sources]+['-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);lib.head_presentation_test.argtypes=[words,words,ctypes.POINTER(ctypes.c_double),ctypes.POINTER(ctypes.c_uint8),words];lib.head_presentation_test.restype=ctypes.c_uint;return lib

def run(lib,c):
 a=[c[k]for k in ['operation','imperial','language','extra']]+c['rect'];args=(ctypes.c_uint32*len(a))(*a);words=(ctypes.c_uint32*len(WORDS))(*[c[f[0]]for f in WORDS]);values=(ctypes.c_double*len(DOUBLES))(*[c[f[0]]for f in DOUBLES]);pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*4)()
 result=lib.head_presentation_test(args,words,values,pixels,out)
 if result:raise ValueError((result,c))
 return dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out))
