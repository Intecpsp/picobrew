import ctypes,os,shlex,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
 library=Path(directory)/'hardware-console.so'
 sources=['src/hardware_console_tests.cpp','src/eeprom.cpp','src/temperature.cpp','src/enums.cpp','tests/hardware_console_bridge.cpp']
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p)for p in sources]+['-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);lib.hardware_console_test_native.argtypes=[words,words,ctypes.c_void_p];lib.hardware_console_test_native.restype=ctypes.c_uint;return lib

def run(lib,c):
 args=(ctypes.c_uint32*4)(*[c[k]for k in ('operation','clock','counter','water')]);out=(ctypes.c_uint32*2)();events=ctypes.create_string_buffer(8192)
 result=lib.hardware_console_test_native(args,out,events)
 if result:raise ValueError((result,c))
 return dict(state=list(out),events=events.value.decode().splitlines())
