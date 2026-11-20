import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
    library=Path(directory)/'head-quantity.so'
    sources=['src/head_quantity.cpp','src/quantity.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_quantity_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);native.head_quantity_test.argtypes=[words,ctypes.c_double,ctypes.POINTER(ctypes.c_uint8),words,ctypes.POINTER(ctypes.c_double),ctypes.c_void_p];native.head_quantity_test.restype=ctypes.c_uint;return native
def run(native,case):
    values=[int(case[k])&0xffffffff for k in ['operation','id','unit','metric','precision']]+case['rect'];args=(ctypes.c_uint32*len(values))(*values);pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*10)();scalars=(ctypes.c_double*2)();text=ctypes.create_string_buffer(32)
    status=native.head_quantity_test(args,case['value'],pixels,out,scalars,text)
    if status:raise ValueError(f'Unexpected quantity status {status}: {case}')
    if case['operation']:return dict(text=text.value.decode())
    return dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out),minimum=scalars[0],step=scalars[1])
def compared(original):return original
