"""Replay full Runtime continuations, reusing the established state projection."""
import ctypes,json,os,shlex,subprocess
from pathlib import Path
import program_runtime_cases as inputs
import test_program_runtime as legacy
ROOT=legacy.ROOT

def bridge_source():
 s=legacy.bridge_source().replace('#include <vector>','#include <vector>\n#include <string>')
 s=s.replace('const ProgramView* program; };','const ProgramView* program; Word counter,last_assertion; };\nstatic std::string diagnostic_events,log_events;')
 s=s.replace('Fixture context{2000,true,{},nullptr};','Fixture context{2000,true,{},nullptr,0xffffffffu,0};').replace('Fixture context{now,is_ready!=0,{},nullptr};','Fixture context{now,is_ready!=0,{},nullptr,0xffffffffu,0};diagnostic_events.clear();log_events.clear();')
 marker='static Word millis(void* p)'
 callbacks='''static bool assertion(void* p,const char* file,Word line,const char* expression){auto& c=fixture(p);event(p,30,line);diagnostic_events+=std::string(file)+"|"+std::to_string(line)+"|"+expression+"\\n";c.last_assertion=line;++c.counter;c.now+=200;return true;}
static bool log(void* p,ConsoleLevel level,const char* tag,const char* file,Word line,const char* message){event(p,31,line,static_cast<Word>(level));log_events+=std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(line)+"|"+message+"\\n";return true;}
'''
 s=s.replace(marker,callbacks+marker)
 s=s.replace('runtime.services.context=&context;runtime.services.millis=millis;','runtime.services.context=&context;runtime.services.millis=millis;runtime.services.diagnostics.context=&context;runtime.services.diagnostics.assertion=assertion;runtime.services.diagnostics.log=log;')
 s=s.replace('result=runtime.stack.push({static_cast<OpCode>(args[0]),args[1]});','result=runtime.stack.push({static_cast<OpCode>(args[0]),args[1]},&runtime.services.diagnostics);')
 s=s.replace('result=runtime.stack.pop(popped);','result=runtime.stack.pop(popped,&runtime.services.diagnostics);')
 s=s.replace('static_cast<Unit>(args[1]),output))','static_cast<Unit>(args[1]),output,&runtime.services.diagnostics))')
 s=s.replace('metadata[0]=runtime.assertion_line;','metadata[0]=context.last_assertion;metadata[6]=context.counter;metadata[7]=context.now;')
 s+='\nextern "C" const char* runtime_diagnostic_events(){return diagnostic_events.c_str();}\nextern "C" const char* runtime_log_events(){return log_events.c_str();}\n'
 s+=r'''
extern "C" unsigned runtime_continuation_service_contracts(){
    ConsoleServices failed;failed.assertion=[](void*,const char*,Word,const char*){return false;};failed.log=[](void*,ConsoleLevel,const char*,const char*,Word,const char*){return false;};
    ConsoleServices missing;missing.log=failed.log;ConsoleServices okay=failed;okay.assertion=[](void*,const char*,Word,const char*){return true;};okay.log=[](void*,ConsoleLevel,const char*,const char*,Word,const char*){return true;};
    unsigned result=0;InstructionStack stack;for(unsigned i=0;i<256;++i)stack.push({OpCode::Noop,0});
    if(stack.push({OpCode::WaitFor,3},&failed)==RuntimeResult::ServiceFailure&&stack.size()==256)result|=1;
    InstructionStack empty;Instruction out{OpCode::WaitFor,3};if(empty.pop(out,&missing)==RuntimeResult::MissingService&&out.opcode==OpCode::WaitFor)result|=2;
    double value=99;if(!quantity_in({3,Unit::Liter},Unit::Second,value,&failed)&&value==0)result|=4;
    ProgramRuntime runtime;runtime.services.diagnostics=failed;runtime.state.instruction_date=10;bool done=true;if(runtime.pending_complete(RuntimeCommandId::None,0,done)==RuntimeResult::ServiceFailure&&!done)result|=8;
    TemperatureDropWatch watch;if(drop_watch_start(watch,2,&failed)==RuntimeResult::ServiceFailure&&!watch.enabled)result|=16;
    watch.enabled=true;if(drop_watch_update(watch,10,&failed)==RuntimeResult::ServiceFailure&&watch.maximum==10)result|=32;
    if(drop_watch_stop(watch,&failed)==RuntimeResult::ServiceFailure&&watch.enabled)result|=64;
    if(drop_watch_start(watch,1,&failed)==RuntimeResult::Ok&&watch.maximum==10)result|=128;
    if(empty.push_checked({static_cast<OpCode>(999),0},&okay)==RuntimeResult::Ok&&empty.size()==1)result|=256;
    okay.log=failed.log;if(!quantity_in({3,Unit::Liter},Unit::Second,value,&okay)&&value==0)result|=512;
    return result;
}
'''

 return s

def load_native(directory):
 build=Path(directory);bridge=build/'runtime-continuation.cpp';bridge.write_text(bridge_source());msp=build/'msp.o';library=build/'runtime-continuation.so'
 warnings=['-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-I',str(ROOT/'include')]
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11']+warnings+['-c',str(ROOT/'src/msp_protocol.c'),'-o',str(msp)],check=True)
 names=['program_runtime','runtime_instruction','program_runtime_execution','quantity','program_runtime_heater','heater','enums']
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-fno-fast-math','-ffp-contract=off']+warnings+['-shared',str(bridge)]+[str(ROOT/'src'/(n+'.cpp'))for n in names]+[str(msp),'-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));word,real=ctypes.c_uint32,ctypes.c_double
 lib.run_case.argtypes=[word,ctypes.POINTER(word),ctypes.POINTER(word),ctypes.POINTER(real),ctypes.POINTER(word),word,word,word,ctypes.POINTER(word),ctypes.POINTER(real),ctypes.POINTER(word),ctypes.POINTER(word),ctypes.POINTER(ctypes.c_char),ctypes.POINTER(word)]
 lib.run_case.restype=None;lib.runtime_diagnostic_events.restype=ctypes.c_char_p;lib.runtime_log_events.restype=ctypes.c_char_p;return lib

def run(lib,case):
 word,real=ctypes.c_uint32,ctypes.c_double
 words=(word*len(inputs.WORDS))(*(case['words'].get(f['name'],f['default'])for f in inputs.WORDS));doubles=(real*len(inputs.DOUBLES))(*(case['doubles'].get(f['name'],f['default'])for f in inputs.DOUBLES))
 args=(word*4)(*(case['args']+[0]*(4-len(case['args']))));stack=(word*512)(*(v for pair in case['stack']for v in pair));outw=(word*len(inputs.WORDS))();outd=(real*len(inputs.DOUBLES))();outs=(word*512)();oute=(word*4096)();meta=(word*8)();names=ctypes.create_string_buffer(768)
 lib.run_case(case['action'],args,words,doubles,stack,len(case['stack']),case['now'],case['ready'],outw,outd,outs,oute,names,meta)
 if meta[1]!=0:raise RuntimeError(('Native continuation failed',meta[1],case))
 return dict(assertion=meta[0],counter=meta[6],clock=meta[7],diagnostics=lib.runtime_diagnostic_events().decode().splitlines(),logs=lib.runtime_log_events().decode().splitlines(),value=list(meta[4:6]),words={f['name']:outw[i]for i,f in enumerate(inputs.WORDS)if f['output']},doubles={f['name']:outd[i]for i,f in enumerate(inputs.DOUBLES)if f['output']},stack=[list(outs[2*i:2*i+2])for i in range(meta[2])],events=[list(oute[4*i:4*i+4])for i in range(meta[3])],names=[names.raw[256*i:256*(i+1)].split(b'\0',1)[0].decode()for i in range(3)])
