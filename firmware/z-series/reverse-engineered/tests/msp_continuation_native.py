import ctypes as C,os,shlex,subprocess
from pathlib import Path
from test_msp_protocol import State,Client,Transport,Segment,Image,array,TX,RX,DELAY
ROOT=Path(__file__).resolve().parents[1]
ASSERT=C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_char_p,C.c_uint32,C.c_char_p)
LOG=C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_uint,C.c_char_p,C.c_char_p,C.c_uint32,C.c_char_p)
SCOPE=C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_bool,C.c_char_p,C.c_char_p,C.c_uint32,C.c_char_p)
COUNT=C.CFUNCTYPE(C.c_uint32,C.c_void_p)
WATCHDOG=C.CFUNCTYPE(C.c_bool,C.c_void_p)
class Diagnostics(C.Structure):
 _fields_=[('context',C.c_void_p),('assertion',ASSERT),('log',LOG),('trace',SCOPE),('assertion_count',COUNT),('watchdog_reset',WATCHDOG)]

def load_native(directory):
 directory=Path(directory);obj=directory/'msp-protocol.o';libpath=directory/'msp-continuation.so'
 flags=['-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-I'+str(ROOT/'include')]
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11']+flags+['-c',str(ROOT/'src/msp_protocol.c'),'-o',str(obj)],check=True)
 sources=['src/msp_console_tests.cpp','src/msp_images.cpp','tests/msp_continuation_bridge.cpp']
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11']+flags+['-shared']+[str(ROOT/s)for s in sources]+[str(obj),'-o',str(libpath)],check=True)
 lib=C.CDLL(str(libpath));pc=C.POINTER(Client);pi=C.POINTER(Image)
 lib.z_msp_init.argtypes=[pc,C.POINTER(State),Transport,C.c_uint8,C.c_uint32,C.c_uint32,C.c_uint32]
 lib.z_msp_send.argtypes=[pc,C.c_uint8,C.c_uint32,C.c_uint]
 lib.z_msp_move_valve.argtypes=[pc,C.c_uint8]
 lib.z_msp_program_segment.argtypes=[pc,C.c_uint32,C.POINTER(C.c_uint8),C.c_uint32]
 lib.z_msp_update.argtypes=[pc,pi];lib.z_msp_setup.argtypes=[pc,pi]
 for name in ('z_msp_start_app','z_msp_get_version','z_msp_erase_app'):getattr(lib,name).argtypes=[pc]
 lib.msp_production_image.restype=pi;lib.msp_unit_image.restype=pi
 lib.msp_continuation_test.argtypes=[pc,C.c_uint32,SCOPE];lib.msp_continuation_test.restype=C.c_bool
 return lib

def run(lib,case,missing=None):
 events=[];responses=list(case['responses']);errors=[];clock=case['clock'];counter=case['counter'];state=State.from_original(case['before'])
 def decode(value):return value.decode()if value else ''
 def delay(context,value):
  nonlocal clock
  events.append(f'delay|{value}');clock=(clock+value)&0xffffffff
 def assertion(context,file,line,expression):
  nonlocal counter
  events.append(f'assertion|{decode(file)}|{line}|{decode(expression)}');counter=(counter+1)&0xffffffff;delay(context,200);return True
 def log(context,level,tag,file,line,message):events.append(f'log|{level}|{decode(tag)}|{decode(file)}|{line}|{decode(message)}');return True
 def trace(context,begin,tag,file,line,name):events.append(f'trace|{int(begin)}|{decode(tag)}|{decode(file)}|{line}|{decode(name)}');return True
 def scope(context,begin,tag,file,line,name):
  events.append(f'scope|{int(begin)}|{decode(tag)}|{decode(file)}|{line}|{decode(name)}')
  if not begin and case['water']<=700:assertion(context,b'Assert.h',0x48,b'stackHighWaterMark > 700')
  return True
 def watchdog_reset(context):events.append('watchdog_reset');return True
 def transmit(context,address,data,count):events.append(f'tx|{address}|{C.string_at(data,count).hex()}')
 def receive(context,address,data,count):
  if not responses:errors.append('Unprovided native response');payload=bytes(count)
  else:
   payload=bytes.fromhex(responses.pop(0))
   if len(payload)!=count:errors.append(f'Native response count {count}, provided {len(payload)}');payload=(payload+bytes(count))[:count]
  events.append(f'rx|{address}|{count}|{payload.hex()}');C.memmove(data,payload,count)
 tx,rx,dl=TX(transmit),RX(receive),DELAY(delay);transport=Transport(None,tx,rx,dl);client=Client()
 assert lib.z_msp_init(C.byref(client),C.byref(state),transport,0x48,41,17,23)==0
 af,lf,tf,cf,ff,sf=ASSERT(assertion),LOG(log),SCOPE(trace),COUNT(lambda p:counter),WATCHDOG(watchdog_reset),SCOPE(scope)
 diagnostics=Diagnostics(None,af,lf,tf,cf,ff)
 if missing:setattr(diagnostics,missing,type(getattr(diagnostics,missing))())
 client.diagnostics=C.cast(C.pointer(diagnostics),C.c_void_p)
 production=lib.msp_production_image();storage=[]
 if case['corrupt_crc']:
  copied=(Segment*production.contents.segment_count)()
  for i in range(len(copied)):
   source=production.contents.segments[i];raw=bytearray(C.string_at(source.bytes,source.size))
   if i==0:raw[3]^=1
   storage.append(array(raw));copied[i]=Segment(source.address,source.size,storage[-1])
  replacement=Image(len(copied),copied);production=C.pointer(replacement)
 operation=case['operation']
 if operation=='send':result=lib.z_msp_send(C.byref(client),case['command'],case['value'],case['value_bytes'])
 elif operation=='valve':result=lib.z_msp_move_valve(C.byref(client),case['position'])
 elif operation=='start':result=lib.z_msp_start_app(C.byref(client))
 elif operation=='version':result=lib.z_msp_get_version(C.byref(client))
 elif operation=='erase':result=lib.z_msp_erase_app(C.byref(client))
 elif operation=='program':
  payload=bytes.fromhex(case['data']);buffer=array(payload);result=lib.z_msp_program_segment(C.byref(client),case['address'],buffer,len(payload))
 elif operation=='update':
  a=array(bytes(range(17)));b=array(b'\xa5');segments=(Segment*2)(Segment(0xc002,17,a),Segment(0xfffc,1,b));image=Image(2,segments);result=lib.z_msp_update(C.byref(client),C.byref(image))
 elif operation=='setup':result=lib.z_msp_setup(C.byref(client),production)
 else:
  callback=dict(test_version=0x400dc1a4,test_switch=0x400dc970,test_update=0x400dd268,test_integration=0x400dd29c,test_bootstrap=0x400dd0f0,test_errors=0x400dccf0)[operation]
  result=0 if lib.msp_continuation_test(C.byref(client),callback,sf)else -11
 if errors:raise ValueError((case['name'],errors[:5]))
 return dict(result=result,state=state.to_original(case['before']),response=bytes(client.response).hex(),counter=counter,clock=clock,remaining=len(responses),events=events)
