"""Native fixture for recovered Head event bodies at explicit service boundaries."""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def load_native(directory):
    library = Path(directory) / "head-events.so"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    subprocess.run(compiler + ["-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-fPIC", "-shared", "-I" + str(ROOT / "include"), str(ROOT / "src/head_events.cpp"),
                   str(ROOT / "tests/head_events_bridge.cpp"), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    pointer = ctypes.POINTER(ctypes.c_uint32)
    native.head_events_test.argtypes = [ctypes.c_uint, pointer, pointer, pointer]
    native.head_events_test.restype = ctypes.c_int
    return native


def run(native, item):
    words = (ctypes.c_uint32 * 9)(*item["words"])
    state, output = (ctypes.c_uint32 * 3)(), (ctypes.c_uint32 * 16)()
    result = native.head_events_test(item["operation"], words, state, output)
    if result:
        raise ValueError("Native Head event service failed: " + str(result))
    return dict(notification=state[0], hijacked=state[1],
                effects=[list(output[i:i + 2]) for i in range(0, state[2], 2)])


def compared(original):
    # The handler API receives an event already selected/consumed by HeadFlow's
    # service adapter. Original queue consumption is retained as extra evidence,
    # not falsely attributed to these event bodies.
    return {key: original[key] for key in ("notification", "hijacked", "effects")}
