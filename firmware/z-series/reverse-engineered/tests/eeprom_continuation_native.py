import ctypes as C,os,shlex,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def load_native(directory):
 library=Path(directory)/'eeprom-continuation.so'
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-shared','-I'+str(ROOT/'include'),str(ROOT/'src/eeprom.cpp'),str(ROOT/'tests/eeprom_continuation_bridge.cpp'),'-o',str(library)],check=True)
 lib=C.CDLL(str(library));byte=C.POINTER(C.c_uint8);word=C.POINTER(C.c_uint32);lib.eeprom_continuation_native.argtypes=[C.c_uint,byte,byte,word,C.c_char_p,byte,byte,word,C.c_void_p];lib.eeprom_continuation_native.restype=C.c_uint;return lib
def run(lib,case):
 byte=C.c_uint8;word=C.c_uint32;data=(byte*120).from_buffer_copy(bytes.fromhex(case['data']));cache=(byte*120).from_buffer_copy(bytes.fromhex(case['cache']));args=(word*8)(*[case[k]for k in ('initialized','dirty','requested','actual','begin','commit','counter','clock')]);out_data=(byte*120)();out_cache=(byte*120)();meta=(word*6)();events=C.create_string_buffer(32768)
 result=lib.eeprom_continuation_native(case['action'],data,cache,args,None if case['text']is None else case['text'].encode(),out_data,out_cache,meta,events)
 if result:raise ValueError(('Native EEPROM result',result,case))
 return dict(assertion=meta[0],value=meta[1],data=bytes(out_data).hex(),cache=bytes(out_cache).hex(),initialized=meta[2],dirty=meta[3],counter=meta[4],clock=meta[5],events=events.value.decode().splitlines())
