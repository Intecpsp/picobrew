"""Native comparison of the complete reconstructed Runtime event mailbox."""
import ctypes
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def load_native(directory):
    library = Path(directory) / "events.so"
    subprocess.run(["c++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-shared", "-fPIC", "-I" + str(ROOT / "include"),
                    str(ROOT / "src/runtime_events.cpp"), str(ROOT / "tests/runtime_events_bridge.cpp"),
                    "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.runtime_events_mailbox.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    return native


def run(native, case):
    words = (ctypes.c_uint32 * 5)(*case["words"])
    output = (ctypes.c_uint32 * 6)()
    if native.runtime_events_mailbox(words, output):
        raise ValueError("Native Runtime event mailbox did not yield an event")
    return dict(fields=list(output[:4]), pending_after_put=output[4], pending_after_take=output[5])
