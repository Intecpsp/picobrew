"""Build and invoke reconstructed Head numeric input bodies."""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def load_native(directory):
    library = Path(directory) / "head-numeric.so"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    subprocess.run(compiler + ["-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-fPIC", "-shared", "-I" + str(ROOT / "include"), str(ROOT / "src/head_numeric_inputs.cpp"),
                   str(ROOT / "tests/head_numeric_bridge.cpp"), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library)); pointer = ctypes.POINTER(ctypes.c_uint32)
    native.head_numeric_test.argtypes = [ctypes.c_uint, pointer, pointer, pointer, pointer]
    native.head_numeric_test.restype = ctypes.c_uint
    return native


def run(native, item):
    state = (ctypes.c_uint32 * 49)(*item["state"])
    value = (ctypes.c_uint32 * 4)(*item["value"])
    result, output = (ctypes.c_uint32 * 52)(), (ctypes.c_uint32 * 24)()
    status = native.head_numeric_test(item["operation"], state, value, result, output)
    if status != 0:
        raise ValueError("Native Head numeric service failed: " + str(status))
    return dict(state=list(result[:49]), assertion_line=result[49], assertion_count=result[51],
                effects=[list(output[i:i + 6]) for i in range(0, result[50], 6)])


def compared(original):
    return {key: original[key] for key in ("state", "assertion_line", "assertion_count", "effects")}
