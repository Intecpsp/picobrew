import ctypes,json,os,shlex,subprocess
from pathlib import Path
from program_recipe_native import dependency
ROOT=Path(__file__).resolve().parents[1]
SOURCES = [
    'src/basic_console_tests.cpp', 'adapters/arduinojson/basic_console_tests_json.cpp',
    'src/backend_console_tests.cpp', 'src/console_test_support.cpp',
    'src/program_diagnostics.cpp', 'src/http_backend.cpp', 'src/backend_diagnostics.cpp',
    'adapters/arduinojson/http_backend_json.cpp', 'adapters/arduinojson/z_backend_json.cpp',
    'adapters/arduinojson/backend_console_tests_json.cpp',
    'src/program_list.cpp', 'src/program_recipe.cpp',
    'src/software_console_tests.cpp', 'src/hardware_console_tests.cpp',
    'src/eeprom.cpp', 'src/temperature.cpp', 'src/still_client.cpp',
    'src/still_authorization.cpp', 'adapters/arduinojson/still_authorization_json.cpp',
    'src/program_runtime.cpp', 'src/runtime_instruction.cpp', 'src/program_repository.cpp',
    'src/program_catalog.cpp', 'src/program_writer.cpp', 'src/program_runtime_execution.cpp',
    'src/head_dynamic.cpp', 'src/head_quantity.cpp', 'src/head_screens.cpp',
    'src/display_text.cpp', 'src/display.cpp', 'src/safeties.cpp',
    'src/serial_logger.cpp', 'src/core_csv.cpp', 'src/pico_still_sensors.cpp',
    'src/wifi.cpp', 'src/quantity.cpp', 'src/local_http.cpp', 'src/http_client.cpp',
    'src/enums.cpp', 'tests/basic_console_bridge.cpp',
]
def load_native(directory):
 library=Path(directory)/'basic-console.so'
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-shared','-fPIC','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'include'),'-I'+str(dependency()/'src')]+[str(ROOT/p)for p in SOURCES]+['-Wl,--gc-sections','-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);lib.basic_console_test_native.argtypes=[words,words,ctypes.c_void_p,ctypes.c_size_t];lib.basic_console_test_native.restype=ctypes.c_uint;return lib

def run(lib,c,allow_failure=False):
 args=(ctypes.c_uint32*4)(int(c['callback'],16),c['clock'],c['counter'],c['water']);out=(ctypes.c_uint32*2)();events=ctypes.create_string_buffer(1048576)
 result=lib.basic_console_test_native(args,out,events,len(events))
 if result and not allow_failure:raise ValueError((result,c,events.value.decode()[:3000]))
 return dict(state=list(out),events=json.loads(events.value),**({'result':result}if allow_failure else{}))
