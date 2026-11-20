"""Native Azure diagnostic bridge; key and signature traces remain hashed."""
from pathlib import Path
import ctypes
import hashlib
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import recover_backend_credentials

def build(output):
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True);recover_backend_credentials.generate(output/'generated')
    library=output/'azure-console.so';sources=[ROOT/('src/'+name+'.cpp') for name in ('azure_console_tests','azure','enums','diagnostics')]
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-I'+str(ROOT/'include'),'-I'+str(output/'generated'),*map(str,sources),str(ROOT/'tests/azure_console_bridge.cpp'),'-lcrypto','-o',str(library)],check=True)
    fn=ctypes.CDLL(str(library)).azure_console_case
    fn.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_uint,ctypes.c_uint,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint32),ctypes.c_size_t]
    fn.restype=ctypes.c_int;return fn
def execute(fn,case,missing=0,failure_kind=0,failure_at=0):
    words=(ctypes.c_uint32*len(case['words']))(*case['words']);output=(ctypes.c_uint32*1000000)()
    if fn(words,missing,failure_kind,failure_at,output,len(output)):raise ValueError('Azure native bridge failed; private trace omitted')
    values=list(output[1:output[0]+1]);result=values[0];state=values[1:19];count=values[19];offset=20;events=[]
    for _ in range(count):
        size=values[offset];offset+=1;row=values[offset:offset+size];offset+=size
        n=row[3];text=bytes(row[4:4+n]);at=4+n;data=bytes(row[at+1:at+1+row[at]])
        if len(row)!=at+1+len(data):raise ValueError('Malformed private Azure trace')
        events.append(row[:3]+[len(text),hashlib.sha256(text).hexdigest(),len(data),hashlib.sha256(data).hexdigest()])
    if offset!=len(values):raise ValueError('Trailing Azure trace data')
    return dict(id=case['id'],result=result,state=state,events=events)
