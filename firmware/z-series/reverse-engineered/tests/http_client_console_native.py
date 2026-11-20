"""Native live-HTTP diagnostic bridge; raw private strings never leave memory."""
from pathlib import Path
import ctypes
import hashlib
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import recover_backend_credentials

def build(output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    recover_backend_credentials.generate(output/'generated')
    library=output/'http-client-console.so'
    sources=[ROOT/('src/'+name+'.cpp') for name in ('http_client_console_tests','http_client','enums','diagnostics')]
    sources.append(ROOT/'tests/http_client_console_bridge.cpp')
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
                    '-I'+str(ROOT/'include'),'-I'+str(output/'generated'),*map(str,sources),'-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));fn=native.http_client_console_case
    fn.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_char_p,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint,
                 ctypes.POINTER(ctypes.c_uint32),ctypes.c_size_t];fn.restype=ctypes.c_int
    return fn

def execute(fn,case,missing=0,failure_kind=0,failure_at=0):
    words=(ctypes.c_uint32*len(case['words']))(*case['words']);output=(ctypes.c_uint32*2000000)()
    status=fn(words,case['wire'].encode('latin1'),missing,failure_kind,failure_at,output,len(output))
    if status:raise ValueError('Native live HTTP bridge failed without exposing private traces')
    values=list(output[1:output[0]+1]);result=values[0];state=values[1:13];count=values[13];offset=14;events=[]
    for _ in range(count):
        length=values[offset];offset+=1;row=values[offset:offset+length];offset+=length
        size=row[3];text=bytes(row[4:4+size]);at=4+size;data=bytes(row[at+1:at+1+row[at]])
        if len(row)!=at+1+len(data):raise ValueError('Malformed private trace')
        events.append(row[:3]+[len(text),hashlib.sha256(text).hexdigest(),len(data),hashlib.sha256(data).hexdigest()])
    if offset!=len(values):raise ValueError('Trailing private trace words')
    return dict(id=case['id'],result=result,state=state,events=events)
