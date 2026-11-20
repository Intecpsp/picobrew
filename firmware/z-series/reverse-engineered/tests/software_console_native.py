import ctypes,os,shlex,subprocess
from pathlib import Path
from program_recipe_native import dependency
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['src/software_console_tests.cpp','src/program_runtime.cpp','src/program_runtime_execution.cpp','src/runtime_instruction.cpp','src/program_repository.cpp','src/program_catalog.cpp','src/head_dynamic.cpp','src/serial_logger.cpp','src/core_csv.cpp','src/still_client.cpp','src/pico_still_sensors.cpp','src/still_authorization.cpp','adapters/arduinojson/still_authorization_json.cpp','src/enums.cpp','tests/software_console_bridge.cpp']
def load_native(directory):
 library=Path(directory)/'software-console.so'
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-shared','-fPIC','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'include'),'-I'+str(dependency()/'src')]+[str(ROOT/p)for p in SOURCES]+['-Wl,--gc-sections','-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);lib.software_console_test_native.argtypes=[words,words,ctypes.c_void_p];lib.software_console_test_native.restype=ctypes.c_uint;return lib

def run(lib,c):
 args=(ctypes.c_uint32*4)(*[c[k]for k in ('operation','clock','counter','water')]);out=(ctypes.c_uint32*2)();events=ctypes.create_string_buffer(32768)
 result=lib.software_console_test_native(args,out,events)
 if result:raise ValueError((result,c))
 return dict(state=list(out),events=events.value.decode().splitlines())
