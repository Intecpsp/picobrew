import ctypes
from pathlib import Path
import os
import shlex
import subprocess
ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['src/core_console_tests.cpp', 'src/binary_download_console_tests.cpp',
           'src/console_test_support.cpp', 'tests/network_console_bridge.cpp']


def load_native(directory):
    output = Path(directory) / 'network-console.so'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++'))+['-std=c++11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-pedantic', '-shared', '-fPIC', '-fvisibility=hidden', '-ffunction-sections',
        '-fdata-sections', '-I'+str(ROOT / 'include')]+[str(ROOT / p) for p in SOURCES]+
        ['-Wl,--gc-sections', '-o', str(output)], check=True)
    lib = ctypes.CDLL(str(output));words = ctypes.POINTER(ctypes.c_uint32)
    lib.network_console_native.argtypes = [words, words, ctypes.c_void_p]
    lib.network_console_native.restype = ctypes.c_uint
    return lib


def run(lib, case):
    keys = ['operation', 'clock', 'counter', 'network', 'ethernet', 'wifi', 'available',
            'client_class', 'leaf_delay', 'leaf_assertions']
    args = (ctypes.c_uint32 * len(keys))(*(case[key] for key in keys))
    out = (ctypes.c_uint32 * 3)();events = ctypes.create_string_buffer(32768)
    result = lib.network_console_native(args, out, events)
    if result:
        raise RuntimeError((result, case, events.value.decode()))
    return dict(state=list(out), events=events.value.decode().splitlines())
