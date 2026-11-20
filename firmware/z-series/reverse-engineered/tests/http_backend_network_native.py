"""Run the real backend HTTP machine; hash private text before returning evidence."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
from program_recipe_native import dependency
from backend_console_native import SOURCES as BACKEND_SOURCES

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import recover_backend_credentials
SOURCES = [p for p in BACKEND_SOURCES if not p.startswith('tests/')]
SOURCES += ['src/http_backend_console_network.cpp', 'src/http_client.cpp', 'src/diagnostics.cpp',
            'tests/http_backend_network_bridge.cpp']


def load_native(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    # The complete SDK owner already requests this address. Older development
    # generator revisions can still reproduce this isolated proof safely.
    if 0x3f418735 not in recover_backend_credentials.HTTP_CLIENT_TEST_ADDRESSES:
        recover_backend_credentials.HTTP_CLIENT_TEST_ADDRESSES += (0x3f418735,)
    recover_backend_credentials.generate(directory / 'generated')
    library = directory / 'http-backend-network.so'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++'))+[
        '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fno-fast-math', '-ffp-contract=off', '-shared', '-fPIC', '-fvisibility=hidden',
        '-ffunction-sections', '-fdata-sections', '-I'+str(ROOT / 'include'),
        '-I'+str(dependency() / 'src'), '-I'+str(directory / 'generated')]+
        [str(ROOT / p) for p in SOURCES]+['-Wl,--gc-sections', '-o', str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.http_backend_network_native.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint,
                                                  ctypes.c_void_p, ctypes.c_size_t]
    native.http_backend_network_native.restype = ctypes.c_int
    return native


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def project(private):
    events = []
    for event in private['events']:
        if event.startswith('log|'):
            values = event.split('|', 5)
            event = '|'.join(values[:5])+ '|'+digest(values[5])
        elif event.startswith('host|'):
            values = event.split('|', 2)
            event = '|'.join(values[:2])+'|'+digest(values[2])
        elif event.startswith('write_bytes|'):
            payload = event.split('|', 1)[1]
            event = 'write_bytes|'+str(len(payload.encode()))+'|'+digest(payload)
        elif event.startswith('read_bytes|'):
            values = event.split('|', 2)
            event = '|'.join(values[:2])+'|'+str(len(values[2].encode()))+'|'+digest(values[2])
        events.append(event)
    return dict(result=private['result'], state=private['state'],
                identifier_sha256=digest(private['identifier']), events=events)


def run(native, case, missing=0, failure=0):
    output = ctypes.create_string_buffer(4000000)
    result = native.http_backend_network_native(json.dumps(case).encode(), missing, failure,
                                                output, len(output))
    if result:
        raise ValueError('Backend network bridge failed; private traces withheld, status '+str(result))
    return project(json.loads(output.value))
