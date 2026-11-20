import ctypes,os,shlex,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
 p=Path(directory)/'head-remote.so';subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/f)for f in ['src/head_remote_session.cpp','src/quantity.cpp','src/enums.cpp','tests/head_remote_bridge.cpp']]+['-o',str(p)],check=True);lib=ctypes.CDLL(str(p));pointer=ctypes.POINTER(ctypes.c_uint32);lib.head_remote_test.argtypes=[pointer,pointer,ctypes.c_char_p,pointer];lib.head_remote_test.restype=ctypes.c_uint;return lib
def run(lib,case):
 args=(ctypes.c_uint32*7)(*[case[k]for k in ['command','program','session','recipe','clock','activity','quiet']]);values=(ctypes.c_uint32*16)(*case['parameters']);out=(ctypes.c_uint32*1024)();result=lib.head_remote_test(args,values,case['name'].encode(),out)
 if result:raise ValueError((result,case))
 effects=[];i=2
 for _ in range(out[1]):n=out[i];i+=1;effects.append(list(out[i:i+n]));i+=n
 return dict(assertion_count=out[0],effects=effects)
def compared(original):return {key:original[key]for key in ['assertion_count','effects']}
