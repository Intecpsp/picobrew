"""Build the owned buzzer and compare its complete effects and state."""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
ROOT = Path(__file__).resolve().parents[1]


def load_native(directory):
    output = Path(directory) / 'buzzer.so'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic', '-fPIC', '-shared',
        '-I' + str(ROOT / 'include'), str(ROOT / 'src/buzzer.cpp'),
        str(ROOT / 'tests/buzzer_bridge.cpp'), '-o', str(output)], check=True)
    library = ctypes.CDLL(str(output))
    pointer = ctypes.POINTER(ctypes.c_uint32)
    library.buzzer_test.argtypes = [pointer, pointer, ctypes.c_uint, ctypes.c_char_p, pointer, pointer, ctypes.c_uint]
    library.buzzer_test.restype = ctypes.c_uint
    return library


def run(library, case):
    words = (ctypes.c_uint32 * 12)(*case['state'])
    actions = (ctypes.c_uint32 * (2 * len(case['actions'])))(*(x for action in case['actions'] for x in action))
    output, events = (ctypes.c_uint32 * 15)(), (ctypes.c_uint32 * 2048)()
    status = library.buzzer_test(words, actions, len(case['actions']), case['text'].encode(), output, events, 2048)
    if status:
        raise RuntimeError('Native buzzer bridge failed: ' + str(status))
    return dict(state=list(output[:12]), result=output[12], assertion_line=output[13],
                effects=[list(events[i:i+6]) for i in range(0, output[14], 6)])
