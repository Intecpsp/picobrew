"""Compare reconstructed runtime state, expansion stack and service order to OEM instructions."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

import program_runtime_cases as inputs
import run_msp_emulation as common

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = r'''
#include "zseries/program_runtime.hpp"
#include "zseries/program_runtime_heater.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
using namespace zseries::oem;
using Word=std::uint32_t;
using Event=std::array<Word,4>;
struct Fixture { Word now; bool ready; std::vector<Event> events; const ProgramView* program; };
static Fixture& fixture(void* p) { return *static_cast<Fixture*>(p); }
static void event(void* p, Word kind, Word a=0, Word b=0, Word c=0) {
    fixture(p).events.push_back(Event{{kind,a,b,c}});
}
static Word millis(void* p) { return fixture(p).now; }
static bool enable(void* p,bool v) { event(p,1,v);return true; }
static bool start(void* p) { event(p,2);return true; }
static bool stop(void* p) { event(p,3);return true; }
static bool target(void* p,double v) { std::uint64_t b;std::memcpy(&b,&v,8);event(p,4,b,b>>32);return true; }
static void command(void* p,Word a,Word b) { event(p,5,a,b); }
static bool ready(void* p) { event(p,6);return fixture(p).ready; }
static void cancel(void* p) { event(p,7); }
static void notify(void* p,RuntimeEventType type,Word a,Word b) { event(p,9,static_cast<Word>(type),a,b); }
static const ProgramView* find_program(void* p,Word id,double& rate) {
    event(p,12,id);rate=1.625;return id==0xffffffffu?nullptr:fixture(p).program;
}
static void requeue(void* p,const RuntimeCommand& command) { event(p,11,static_cast<Word>(command.id),command.duration,command.location); }
static void transmit(void* p,std::uint8_t,const std::uint8_t* b,std::size_t n) {
    if(n!=5 && n!=6) { event(p,255,static_cast<Word>(n));return; }
    event(p,10,b[2],n==6?b[3]:0,static_cast<Word>(n-5));
}
static void receive(void*,std::uint8_t,std::uint8_t* b,std::size_t n) { std::memset(b,0,n); }
static void delay(void*,Word) {}
static void gpio(void* p,std::uint8_t pin,bool high) { event(p,20,pin,high); }
extern "C" int heater_integration() {
    Fixture context{2000,true,{},nullptr};ProgramRuntime runtime;
    runtime.services.context=&context;runtime.services.millis=millis;
    z_msp_state controller{};controller.id=Z_MSP_APP_RUNNING;controller.pumps=0x70;
    z_msp_client client{};z_msp_transport transport{&context,transmit,receive,delay};
    z_msp_init(&client,&controller,transport,0x48,0,0,0);runtime.msp=&client;
    HeaterController heater;HeaterSensors sensors;ThermalSafety safety;RuntimeHeaterBinding port;
    sensors.estimated_boil_celsius=95;
    sensors.temperatures[static_cast<std::size_t>(TempSensorId::Hex)].celsius=71;
    sensors.temperatures[static_cast<std::size_t>(TempSensorId::Wort)].celsius=72;
    sensors.temperatures[static_cast<std::size_t>(TempSensorId::Drain)].celsius=73;
    HeaterIo missing{&context,nullptr,nullptr},io{&context,nullptr,gpio};
    if(bind_runtime_heater(runtime,port,heater,sensors,missing))return 1;
    if(!bind_runtime_heater(runtime,port,heater,sensors,io))return 2;
    if(runtime.services.context!=&context || runtime.services.heater_context!=&port)return 3;
    if(runtime.dispatch({OpCode::Heater,1})!=RuntimeResult::Ok || !heater.state.enabled)return 4;
    if(runtime.dispatch({OpCode::SetTarget,98*256})!=RuntimeResult::Ok || heater.state.target_celsius!=93 ||
        heater.state.target_excess!=5 || runtime.observed.heater_target!=93)return 5;
    if(runtime.dispatch({OpCode::StartPid,0})!=RuntimeResult::Ok || !heater.state.started ||
        !heater.pid.automatic || !runtime.state.pid_active)return 6;
    if(runtime.dispatch({OpCode::Pause,0})!=RuntimeResult::Ok || heater.state.started ||
        heater.pid.automatic || !runtime.state.pid_active || !runtime.state.paused)return 7;
    if(runtime.stack.size()!=1 || runtime.stack.at(0).opcode!=OpCode::WaitUser || runtime.stack.at(0).argument!=1)return 8;
    if(runtime.dispatch({OpCode::StopPid,0})!=RuntimeResult::Ok || runtime.state.pid_active)return 9;
    const std::vector<Event> expected={Event{{20,19,1,0}},Event{{20,12,0,0}},Event{{10,13,0x40,1}},Event{{20,12,0,0}}};
    if(context.events!=expected)return 10;
    sensors.estimated_boil_celsius=std::numeric_limits<double>::quiet_NaN();
    if(runtime.dispatch({OpCode::SetTarget,70*256})!=RuntimeResult::ServiceFailure || heater.state.target_celsius!=93)return 11;
    safety.too_hot=2;safety.too_cold=1;sensors.wort_rate=1.25;
    refresh_runtime_thermal_state(runtime,heater,sensors,safety);
    if(runtime.observed.hex_temperature!=71 || runtime.observed.wort_temperature!=72 ||
        runtime.observed.drain_temperature!=73 || runtime.observed.wort_rate!=1.25 ||
        !runtime.observed.too_hot || !runtime.observed.too_cold)return 12;
    return 0;
}
extern "C" void run_case(Word action,const Word* args,const Word* words,const double* doubles,
                         const Word* initial_stack,Word initial_count,Word now,Word is_ready,
                         Word* output_words,double* output_doubles,Word* output_stack,
                         Word* output_events,char* output_names,Word* metadata) {
    Fixture context{now,is_ready!=0,{},nullptr};
    z_msp_state controller{};controller.id=Z_MSP_APP_RUNNING;
    z_msp_client client{};
    z_msp_transport transport{&context,transmit,receive,delay};
    z_msp_init(&client,&controller,transport,0x48,0,0,0);
    ProgramRuntime runtime;runtime.msp=&client;
    RuntimeCommand command_value;
    runtime.services.context=&context;runtime.services.millis=millis;
    runtime.services.heater_enable=enable;runtime.services.pid_start=start;runtime.services.pid_stop=stop;
    runtime.services.heater_target=target;runtime.services.still_command=command;
    runtime.services.still_ready=ready;runtime.services.still_cancel=cancel;
    runtime.services.event=notify;runtime.services.find_program=find_program;runtime.services.requeue_command=requeue;
    static const char* const strings[]={@STRINGS@};
    runtime.program.strings=strings;runtime.program.string_count=sizeof(strings)/sizeof(strings[0]);
    static const Instruction program_code[]={{OpCode::StepName,0},{OpCode::WaitFor,30},{OpCode::Noop,0}};
    runtime.program.name="Fixture program";runtime.program.code=program_code;runtime.program.code_size=3;
    ProgramView available=runtime.program;context.program=&available;
    runtime.state.step_name="initial";runtime.state.substep_name="initial sub";
    std::memcpy(runtime.state.step_buffer.data(),"buffer",7);
    @INITIALIZE@
    runtime.observed.signal_950_defined=runtime.observed.signal_950_defined && runtime.observed.still_sample_present;
    for(Word i=0;i<initial_count;i++)runtime.stack.push({static_cast<OpCode>(initial_stack[2*i]),initial_stack[2*i+1]});
    RuntimeResult result=RuntimeResult::Ok;bool value=false;Word a=0,b=0;
    switch(action){
        case 0:result=runtime.dispatch({static_cast<OpCode>(args[0]),args[1]});break;
        case 1:result=runtime.pending_complete(static_cast<RuntimeCommandId>(args[0]),now,value);a=value;break;
        case 2:a=instruction_duration_ms({static_cast<OpCode>(args[0]),args[1]});break;
        case 3:result=runtime.save_pending(now);break;
        case 4:result=runtime.push_resume(now,args[0]!=0);break;
        case 5:result=runtime.transition(static_cast<RuntimeStateId>(args[0]));break;
        case 6:result=runtime.transition_still(args[0]);break;
        case 7:result=runtime.pause_still(now,args[0]!=0,static_cast<OpCode>(args[1]),args[2],args[3]!=0);break;
        case 8:result=runtime.progress_still(now,static_cast<OpCode>(args[0]),value);a=value;break;
        case 9:result=runtime.update_fan(50);break;
        case 10:result=runtime.push_setup_instructions(args[0]);break;
        case 11:result=runtime.stack.push({static_cast<OpCode>(args[0]),args[1]});
            if(result==RuntimeResult::StackFull)runtime.assertion_line=0x43;
            break;
        case 12:{Instruction popped{OpCode::Noop,0};result=runtime.stack.pop(popped);
            if(result==RuntimeResult::StackEmpty)runtime.assertion_line=0x37;
            else{a=static_cast<Word>(popped.opcode);b=popped.argument;}break;}
        case 13:a=quantity_units_compatible(static_cast<Unit>(args[0]),static_cast<Unit>(args[1]));break;
        case 14:{const std::uint64_t bits=static_cast<std::uint64_t>(args[2])|(static_cast<std::uint64_t>(args[3])<<32);
            double input;std::memcpy(&input,&bits,8);double output;
            if(!quantity_in(Quantity{input,static_cast<Unit>(args[0])},static_cast<Unit>(args[1]),output))runtime.assertion_line=0x7d;
            else{std::uint64_t returned;std::memcpy(&returned,&output,8);a=returned;b=returned>>32;}break;}
        case 15:runtime.update_t1_drop_watch();break;
        case 16:a=runtime.pause_reason(static_cast<RuntimeCommandId>(args[0]));break;
        case 17:result=runtime.select_program(args[0],value);a=value;break;
        case 18:runtime.update_total_time();break;
        case 19:result=runtime.tick(now,command_value);break;
        case 20:{std::vector<Instruction> code;
            for(Word i=0;i<initial_count;i++)code.push_back({static_cast<OpCode>(initial_stack[2*i]),initial_stack[2*i+1]});
            ProgramView candidate=available;candidate.name=args[0]==0?"Fixture program":args[0]==1?"":nullptr;
            if(args[1]==1)candidate.string_count=0;
            if(args[1]==2)candidate.strings=nullptr;
            candidate.code=args[2]==0?code.data():nullptr;candidate.code_size=args[3]?args[3]:code.size();
            const char* diagnostic=program_validation_error(candidate);
            static const char* const messages[]={"Program has no name","Program has no instruction",
                "Program has too many instructions","Program has inconsistent string table","Program has invalid instruction",
                "Program has invalid string reference"};
            if(diagnostic){for(unsigned i=0;i<6;i++)if(std::strcmp(diagnostic,messages[i])==0)a=i+1;}
            break;}
    }
    @OUTPUT@
    for(std::size_t i=0;i<runtime.stack.size();i++){
        output_stack[2*i]=static_cast<Word>(runtime.stack.at(i).opcode);output_stack[2*i+1]=runtime.stack.at(i).argument;
    }
    for(std::size_t i=0;i<context.events.size();i++)for(unsigned j=0;j<4;j++)output_events[4*i+j]=context.events[i][j];
    const char* names[]={runtime.state.step_name,runtime.state.substep_name,runtime.state.step_buffer.data()};
    for(unsigned i=0;i<3;i++)std::strncpy(output_names+256*i,names[i]?names[i]:"<null>",255);
    metadata[0]=runtime.assertion_line;metadata[1]=static_cast<Word>(result);
    metadata[2]=static_cast<Word>(runtime.stack.size());metadata[3]=static_cast<Word>(context.events.size());
    metadata[4]=a;metadata[5]=b;
}
'''


def bridge_source():
    initialize, output = [], []
    for kind, fields in [("words", inputs.WORDS), ("doubles", inputs.DOUBLES)]:
        for index, field in enumerate(fields):
            expression = (field["expression"].replace("command.", "command_value.", 1)
                          if field["expression"].startswith("command.") else "runtime." + field["expression"])
            initialize.append(f"{expression}=static_cast<decltype({expression})>({kind}[{index}]);")
            out_type = "Word" if kind == "words" else "double"
            output.append(f"output_{kind}[{index}]=static_cast<{out_type}>({expression});")
    return BRIDGE.replace("@STRINGS@", ",".join(json.dumps(v) for v in inputs.STRINGS)).replace(
        "@INITIALIZE@", "\n".join(initialize)).replace("@OUTPUT@", "\n".join(output))


class ProgramRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-runtime-native-")
        cls.addClassCleanup(cls.temporary.cleanup)
        build = Path(cls.temporary.name)
        bridge = build / "runtime.cpp"
        bridge.write_text(bridge_source(), encoding="utf-8")
        cc, cxx = shlex.split(os.environ.get("CC", "cc")), shlex.split(os.environ.get("CXX", "c++"))
        warnings = ["-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fPIC", "-I", str(ROOT / "include")]
        subprocess.run(cc + ["-std=c11"] + warnings + ["-c", str(ROOT / "src/msp_protocol.c"), "-o", str(build / "msp.o")],
                       check=True, capture_output=True, text=True)
        subprocess.run(cxx + ["-std=c++11", "-fno-fast-math", "-ffp-contract=off"] + warnings + ["-shared", str(bridge),
                       str(ROOT / "src/program_runtime.cpp"), str(ROOT / "src/runtime_instruction.cpp"),
                       str(ROOT / "src/program_runtime_execution.cpp"), str(ROOT / "src/quantity.cpp"), str(ROOT / "src/enums.cpp"),
                       str(ROOT / "src/program_runtime_heater.cpp"), str(ROOT / "src/heater.cpp"),
                       str(build / "msp.o"), "-o", str(build / "runtime.so")], check=True, capture_output=True, text=True)
        cls.native = ctypes.CDLL(str(build / "runtime.so"))
        word, real = ctypes.c_uint32, ctypes.c_double
        cls.native.run_case.argtypes = [word, ctypes.POINTER(word), ctypes.POINTER(word), ctypes.POINTER(real),
            ctypes.POINTER(word), word, word, word, ctypes.POINTER(word), ctypes.POINTER(real),
            ctypes.POINTER(word), ctypes.POINTER(word), ctypes.POINTER(ctypes.c_char), ctypes.POINTER(word)]
        cls.native.run_case.restype = None
        cls.native.heater_integration.argtypes = []
        cls.native.heater_integration.restype = ctypes.c_int
        fixture = Path(os.environ.get("Z_RUNTIME_FIXTURE", ROOT / "tests/fixtures/program-runtime-original.json"))
        cls.fixture = json.loads(fixture.read_text())
        cls.cases = inputs.cases()
        if cls.fixture["mode"] == "smoke":
            cls.cases = [c for i, c in enumerate(cls.cases) if c["group"] == "dispatch" or i % 31 == 0]

    def actual(self, case):
        word, real = ctypes.c_uint32, ctypes.c_double
        words = (word * len(inputs.WORDS))(*(case["words"].get(f["name"], f["default"]) for f in inputs.WORDS))
        doubles = (real * len(inputs.DOUBLES))(*(case["doubles"].get(f["name"], f["default"]) for f in inputs.DOUBLES))
        args = (word * 4)(*(case["args"] + [0] * (4-len(case["args"]))))
        stack = (word * 512)(*(v for pair in case["stack"] for v in pair))
        output_words, output_doubles = (word * len(inputs.WORDS))(), (real * len(inputs.DOUBLES))()
        output_stack, output_events, metadata = (word * 512)(), (word * 4096)(), (word * 6)()
        names = ctypes.create_string_buffer(768)
        self.native.run_case(case["action"], args, words, doubles, stack, len(case["stack"]), case["now"], case["ready"],
                             output_words, output_doubles, output_stack, output_events, names, metadata)
        self.assertTrue(metadata[1] == 0 or metadata[0] != 0, f"Unexpected portable error {metadata[1]} for {case}")
        return dict(assertion=metadata[0], value=list(metadata[4:6]),
            words={f["name"]: output_words[i] for i,f in enumerate(inputs.WORDS) if f["output"]},
            doubles={f["name"]: output_doubles[i] for i,f in enumerate(inputs.DOUBLES) if f["output"]},
            stack=[list(output_stack[2*i:2*i+2]) for i in range(metadata[2])],
            events=[list(output_events[4*i:4*i+4]) for i in range(metadata[3])],
            names=[names.raw[256*i:256*(i+1)].split(b"\0",1)[0].decode() for i in range(3)])

    def test_dispatch_and_stack_match_original_instructions(self):
        self.assertEqual(len(self.cases), len(self.fixture["cases"]))
        for index, (case, expected) in enumerate(zip(self.cases, self.fixture["cases"])):
            if case["action"] not in (0,2,10,11,12): continue
            with self.subTest(index=index, group=case["group"], args=case["args"]):
                self.assertEqual(self.actual(case), expected)

    def test_waits_transitions_and_still_progress_match_original_instructions(self):
        for index, (case, expected) in enumerate(zip(self.cases, self.fixture["cases"])):
            if case["action"] in (0,2,10,11,12): continue
            with self.subTest(index=index, group=case["group"], args=case["args"]):
                self.assertEqual(self.actual(case), expected)

    def test_fixture_provenance(self):
        if self.fixture["mode"] == "smoke": self.skipTest("Development smoke is not committed evidence")
        fixture = ROOT / "tests/fixtures/program-runtime-original.json"
        report = json.loads((ROOT / "analysis/program-runtime-validation.json").read_text())
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(), report["fixture_sha256"])
        self.assertEqual(len(self.cases), report["cases"])
        raw = common.INPUT.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), report["input_sha256"])
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                    for s in common.images.parse_image(raw)["segments"]]
        for name, expected in report["generator_files"].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected, name)
        for name, expected in report["validated_source_sha256"].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected, name)
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])

    def test_runtime_uses_reconstructed_heater_and_msp_services(self):
        self.assertEqual(self.native.heater_integration(), 0)


if __name__ == "__main__":
    unittest.main()
