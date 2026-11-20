import ctypes,hashlib,os,shlex,subprocess
from pathlib import Path
from head_dynamic_cases import WORDS,DOUBLES
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
 library=Path(directory)/'head-dynamic.so'
 sources=['src/head_dynamic.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_dynamic_bridge.cpp']
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p)for p in sources]+['-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);lib.head_dynamic_test.argtypes=[words,words,ctypes.POINTER(ctypes.c_double)]+[ctypes.c_char_p]*4+[ctypes.POINTER(ctypes.c_uint8),words,ctypes.c_void_p];lib.head_dynamic_test.restype=ctypes.c_uint;return lib
def run(lib,c):
 a=[c[k]for k in ['operation','imperial','language','reason']]+c['rect']+[c['context_font'],c['context_spacing']];args=(ctypes.c_uint32*len(a))(*a);words=(ctypes.c_uint32*len(WORDS))(*[c[f[0]]for f in WORDS]);values=(ctypes.c_double*len(DOUBLES))(*[c[f[0]]for f in DOUBLES]);pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*3)();text=ctypes.create_string_buffer(32)
 result=lib.head_dynamic_test(args,words,values,*[c[k].encode()for k in ['name','step','substep','error']],pixels,out,text)
 if result:raise ValueError((result,c))
 if c['operation']==4:return dict(text=text.value.decode())
 return dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out))
def compared(original):return original
