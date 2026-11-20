import ctypes,hashlib,os,shlex,subprocess
from pathlib import Path
from head_slideshow_cases import WORDS,DOUBLES,FLOATS
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
 library=Path(directory)/'head-slideshow.so'
 sources=['src/head_slideshow.cpp','src/head_presentation.cpp','src/head_dynamic.cpp','src/head_flow.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_slideshow_bridge.cpp']
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p)for p in sources]+['-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);lib.head_slideshow_test.argtypes=[words,words,ctypes.POINTER(ctypes.c_double)]+[ctypes.c_char_p]*3+[ctypes.POINTER(ctypes.c_uint8),words,ctypes.c_void_p,ctypes.c_void_p];lib.head_slideshow_test.restype=ctypes.c_uint;return lib

def run(lib,c,pixels_out=None):
 a=[c[k]for k in ['operation','imperial','language','type','counter','mode','detail','static','select','change','clock','tick','tick2']];args=(ctypes.c_uint32*len(a))(*a);words=(ctypes.c_uint32*len(WORDS))(*[c[f[0]]for f in WORDS]);values=(ctypes.c_double*(len(DOUBLES)+len(FLOATS)))(*[c[f[0]]for f in DOUBLES+FLOATS]);pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*94)();events=ctypes.create_string_buffer(8192);titles=ctypes.create_string_buffer(252)
 result=lib.head_slideshow_test(args,words,values,*[c[k].encode()for k in ['name','step','substep']],pixels,out,events,titles)
 if result:raise ValueError((result,c))
 if pixels_out is not None:pixels_out.append(bytes(pixels))
 result=dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out[:4]),events=events.value.decode().splitlines())
 if c['operation'] not in (0,4):result['slideshow']=list(out[4:10]);result['entries']=[list(out[10+i*7:17+i*7])+[titles.raw[i*21:(i+1)*21].split(b'\0')[0].decode()]for i in range(12)]
 return result
