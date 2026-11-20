import ctypes,os,shlex,subprocess
from pathlib import Path
from program_recipe_native import dependency
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['src/backend_console_tests.cpp','src/console_test_support.cpp','src/program_diagnostics.cpp','src/http_backend.cpp','src/backend_diagnostics.cpp','adapters/arduinojson/http_backend_json.cpp','adapters/arduinojson/z_backend_json.cpp','adapters/arduinojson/backend_console_tests_json.cpp','src/program_list.cpp','src/program_recipe.cpp','src/program_repository.cpp','src/program_writer.cpp','src/program_runtime.cpp','src/program_runtime_execution.cpp','src/runtime_instruction.cpp','src/quantity.cpp','src/enums.cpp','tests/backend_console_bridge.cpp']
def load_native(directory):
 library=Path(directory)/'backend-console.so'
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-shared','-fPIC','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'include'),'-I'+str(dependency()/'src')]+[str(ROOT/p)for p in SOURCES]+['-Wl,--gc-sections','-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);lib.backend_console_test_native.argtypes=[words,words,ctypes.c_void_p];lib.backend_console_test_native.restype=ctypes.c_uint;return lib
def run(lib,c):
 args=(ctypes.c_uint32*7)(c['operation'],c['clock'],c['counter'],c['water'],c.get('argument',c.get('category',0)),c.get('second',c.get('elapsed',0)),c.get('failed',0));out=(ctypes.c_uint32*3)();events=ctypes.create_string_buffer(131072)
 result=lib.backend_console_test_native(args,out,events)
 if result:raise ValueError((result,c,events.value.decode()[-1000:]))
 return dict(state=list(out)[:3 if c['operation']>=3 else 2],events=events.value.decode().splitlines())
