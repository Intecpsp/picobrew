"""Original instruction acceptance for program storage, resolution and generation."""
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

import program_repository_cases as inputs
import run_msp_emulation as common

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = r'''
#include "zseries/program_repository.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>
using namespace zseries::oem;
using Word=std::uint32_t;
static void quoted(std::ostream& out,const char* text){
    out << '"';if(!text)text="<null>";
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p){
        switch(*p){case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
        case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
        default:if(*p<32)out<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(*p)<<std::dec;else out<<*p;}
    }out << '"';
}
static void bits(std::ostream& out,double value){
    std::uint64_t b;std::memcpy(&b,&value,8);out << '"' << std::hex << std::setfill('0') << std::setw(16) << b << std::dec << '"';
}
static void record(std::ostream& out,const ProgramRecord& p){
    out<<"{\"type\":"<<static_cast<Word>(p.type)<<",\"id\":"<<p.unique_id<<",\"name\":";quoted(out,p.view.name);
    out<<",\"code\":[";for(std::size_t i=0;i<p.view.code_size;i++){if(i)out<<',';out<<'['<<static_cast<Word>(p.view.code[i].opcode)<<','<<p.view.code[i].argument<<']';}
    out<<"],\"strings\":[";for(std::size_t i=0;i<p.view.string_count;i++){if(i)out<<',';quoted(out,p.view.strings[i]);}out<<"]}";
}
static void parameters(std::ostream& out,const ProgramParameters& p){
    out<<"{\"id\":"<<static_cast<Word>(p.id)<<",\"abv\":"<<static_cast<Word>(p.abv)<<",\"ibu\":"<<static_cast<Word>(p.ibu);
    const char* names[]={"time","water","intensity","temperature"};const Quantity quantities[]={p.time,p.water,p.intensity,p.temperature};
    for(unsigned i=0;i<4;i++){out<<",\""<<names[i]<<"\":[";bits(out,quantities[i].value);out<<','<<static_cast<Word>(quantities[i].unit)<<']';}
    out<<",\"resumed\":"<<p.resumed_session<<'}';
}
static void stats(std::ostream& out,const ProgramStats& p){
    out<<"{\"rate\":";bits(out,p.heating_rate_celsius_per_minute);out<<",\"duration\":"<<p.duration_seconds<<",\"pressure\":";
    bits(out,p.pressure_pascal);out<<",\"maximum\":";bits(out,p.maximum_temperature_celsius);out<<",\"added\":"<<p.maximum_temperature_added_seconds<<'}';
}
static void bytes(std::ostream& out,const void* data,std::size_t count){
    out<<'"'<<std::hex<<std::setfill('0');auto p=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<count;i++){out<<std::setw(2)<<static_cast<unsigned>(p[i]);}out<<std::dec<<'"';
}
extern "C" int run_case(Word action,Word initial_code,Word initial_strings,const Word* args,const double* real_args,
    const Word* parameter_words,const double* parameter_values,const char* text,const Word* input_code,Word code_count,
    Word null_code,Word imperial,double pressure,double maximum,Word added,char* output,std::size_t capacity){
    ProgramBuffer buffer;ProgramRepository repository(buffer);ProgramStats static_stats;const ProgramRecord* selected=nullptr;
    if(initial_strings){buffer.start(SessionType::Test,77,"seed-00");for(Word i=1;i<initial_strings;i++){
        char seed[32];::snprintf(seed,sizeof(seed),"seed-%02u",i);buffer.append_string(seed);}}
    for(Word i=0;i<initial_code;i++)buffer.append({OpCode::Noop,i});
    buffer.parameters.id=ZProgramId::ManualBrew;buffer.parameters.abv=7;buffer.parameters.ibu=11;
    buffer.parameters.time={600,Unit::Second};buffer.parameters.water={9,Unit::Liter};
    buffer.parameters.intensity={4,Unit::Intensity};buffer.parameters.temperature={70,Unit::Celsius};buffer.parameters.resumed_session=true;
    buffer.stats.heating_rate_celsius_per_minute=2;buffer.stats.duration_seconds=321;buffer.stats.pressure_pascal=1234;
    buffer.stats.maximum_temperature_celsius=88;buffer.stats.maximum_temperature_added_seconds=456;
    ProgramParameters input;input.id=static_cast<ZProgramId>(parameter_words[0]);input.abv=static_cast<std::int32_t>(parameter_words[1]);
    input.ibu=static_cast<std::int32_t>(parameter_words[2]);input.time={parameter_values[0],static_cast<Unit>(parameter_words[3])};
    input.water={parameter_values[1],static_cast<Unit>(parameter_words[4])};input.intensity={parameter_values[2],static_cast<Unit>(parameter_words[5])};
    input.temperature={parameter_values[3],static_cast<Unit>(parameter_words[6])};input.resumed_session=parameter_words[7]!=0;
    std::vector<Instruction> code;for(Word i=0;i<code_count;i++)code.push_back({static_cast<OpCode>(input_code[2*i]),input_code[2*i+1]});
    Instruction nonnull{OpCode::Noop,0};const Instruction* code_pointer=null_code?nullptr:code.empty()?&nonnull:code.data();
    Word low=0,high=0;ProgramBuildResult result=ProgramBuildResult::Ok;
    switch(action){
    case 0:selected=repository.find(args[0]);break;
    case 1:buffer.reset();break;
    case 2:result=buffer.start(static_cast<SessionType>(args[0]),args[1],text);break;
    case 3:result=buffer.append({static_cast<OpCode>(args[0]),args[1]});break;
    case 4:result=buffer.append_string(text);break;
    case 5:result=buffer.set_code(code_pointer,code_count);break;
    case 6:{double value=0;if(!program_heating_rate(real_args[0],value))buffer.assertion_line=0x2b;
        else{std::uint64_t b;std::memcpy(&b,&value,8);low=b;high=b>>32;}break;}
    case 7:{std::int32_t value=0;result=coffee_intensity_minutes(input.intensity,value);
        if(result==ProgramBuildResult::InvalidUnit)buffer.assertion_line=0x1c1;else low=static_cast<Word>(value);break;}
    case 8:result=begin_coffee_program(buffer,text);break;
    case 9:append_cold_coffee_steps(buffer);break;
    case 10:result=append_hot_coffee_steps(buffer,static_cast<std::int32_t>(args[0]),static_cast<std::int32_t>(args[1]));break;
    case 11:result=finish_program(buffer,input,pressure,maximum,added);break;
    case 12:result=build_coffee_program(buffer,input,pressure,imperial!=0);low=result==ProgramBuildResult::Ok;break;
    case 13:result=repository.static_stats(args[0],pressure,maximum,static_stats);
        if(result==ProgramBuildResult::InvalidProgramId){buffer.assertion_line=args[0]<1||args[0]>28?0x67b:0x63b;}break;
    case 14:low=estimate_instructions_seconds(code_pointer,code_count,false,0,0,real_args[0],real_args[1]);break;
    case 15:low=is_buffered_program(args[0]);break;
    }
    (void)result;std::ostringstream out;
    out<<"{\"assertion\":"<<buffer.assertion_line<<",\"value\":["<<low<<','<<high<<"],\"buffer\":";record(out,buffer.record());
    out<<",\"parameters\":";parameters(out,buffer.parameters);out<<",\"stats\":";stats(out,buffer.stats);out<<",\"static_stats\":";stats(out,static_stats);
    out<<",\"selected\":";if(selected)record(out,*selected);else out<<"null";
    out<<",\"code_storage\":";bytes(out,buffer.code_storage().data(),256*8);
    out<<",\"string_storage\":";bytes(out,buffer.string_storage().data(),64*48);out<<'}';
    const std::string content=out.str();if(content.size()+1>capacity)return -1;
    std::memcpy(output,content.c_str(),content.size()+1);return static_cast<int>(content.size());
}
extern "C" int runtime_integration(){
    ProgramBuffer buffer;ProgramRepository repository(buffer);ProgramRuntime runtime;
    runtime.services.context=&repository;runtime.services.find_program=ProgramRepository::runtime_find;
    runtime.services.millis=[](void*)->Word{return 12345;};buffer.stats.heating_rate_celsius_per_minute=1.625;
    for(Word id=1;id<=28;id++){
        if(is_buffered_program(id))continue;
        bool selected=false;if(runtime.select_program(id,selected)!=RuntimeResult::Ok||!selected)return 1;
        const ProgramRecord* program=repository.find(id);
        if(!program||program_validation_error(program->view)||runtime.program_load_result!=RuntimeResult::Ok)return 2;
        if(runtime.state.start_date!=12345||runtime.state.estimated_heating_rate!=1.625)return 3;
        for(std::size_t i=0;i<program->view.code_size;i++){Instruction instruction{OpCode::Noop,0};
            if(runtime.stack.pop(instruction)!=RuntimeResult::Ok||instruction.opcode!=program->view.code[i].opcode||instruction.argument!=program->view.code[i].argument)return 4;}
    }
    bool selected=false;if(runtime.select_program(24,selected)!=RuntimeResult::Ok||!selected||runtime.program_load_result!=RuntimeResult::InvalidProgram)return 5;
    buffer.start(SessionType::LocalBrew,99,"Buffered recipe");buffer.append({OpCode::WaitFor,30});buffer.append({OpCode::CompleteBrew,0});
    if(runtime.select_program(24,selected)!=RuntimeResult::Ok||!selected||runtime.program_load_result!=RuntimeResult::Ok||runtime.stack.size()!=2)return 6;
    for(Word id=20;id<=24;id++)if(repository.find(id)!=&buffer.record())return 7;
    if(runtime.select_program(29,selected)!=RuntimeResult::Ok||selected)return 8;
    return 0;
}
'''


class ProgramRepositoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-repository-native-")
        cls.addClassCleanup(cls.temporary.cleanup)
        build = Path(cls.temporary.name)
        bridge = build / "repository.cpp"
        bridge.write_text(BRIDGE)
        flags = ["-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fPIC", "-I", str(ROOT / "include")]
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + ["-std=c11"] + flags +
            ["-c", str(ROOT / "src/msp_protocol.c"), "-o", str(build / "msp.o")], check=True, capture_output=True, text=True)
        sources = ["program_repository", "program_catalog", "program_writer", "program_runtime", "program_runtime_execution", "runtime_instruction", "quantity"]
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++11", "-fno-fast-math", "-ffp-contract=off"] + flags + ["-shared", str(bridge)] +
            [str(ROOT / f"src/{name}.cpp") for name in sources] + [str(build / "msp.o"), "-o", str(build / "repository.so")], check=True, capture_output=True, text=True)
        cls.native = ctypes.CDLL(str(build / "repository.so"))
        word, real = ctypes.c_uint32, ctypes.c_double
        cls.native.run_case.argtypes = [word, word, word, ctypes.POINTER(word), ctypes.POINTER(real), ctypes.POINTER(word),
            ctypes.POINTER(real), ctypes.c_char_p, ctypes.POINTER(word), word, word, word, real, real, word,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        cls.native.run_case.restype = ctypes.c_int
        cls.native.runtime_integration.argtypes = []
        cls.native.runtime_integration.restype = ctypes.c_int
        fixture = Path(os.environ.get("Z_REPOSITORY_FIXTURE", ROOT / "tests/fixtures/program-repository-original.json"))
        cls.fixture = json.loads(fixture.read_text())
        cls.cases = inputs.cases()
        if cls.fixture["mode"] == "smoke": cls.cases = [c for i, c in enumerate(cls.cases) if c["action"] in (0,13,15) or i % 17 == 0]

    def actual(self, case):
        word, real = ctypes.c_uint32, ctypes.c_double
        args, real_args = (word * 4)(), (real * 2)()
        if case["action"] in (6, 14):
            for i, value in enumerate(case["args"]): real_args[i] = value
        else:
            for i, value in enumerate(case["args"]): args[i] = value
        params = case["parameters"]
        parameter_words = (word * 8)(params["id"], params["abv"], params["ibu"], *(params[k][1] for k in ("time","water","intensity","temperature")), params["resumed"])
        parameter_values = (real * 4)(*(params[k][0] for k in ("time","water","intensity","temperature")))
        code = (word * max(2, 2*len(case["code"])))(*(v for instruction in case["code"] for v in instruction))
        output = ctypes.create_string_buffer(65536)
        size = self.native.run_case(case["action"], case["initial_code"], case["initial_strings"], args, real_args,
            parameter_words, parameter_values, None if case["text"] is None else case["text"].encode(), code,
            len(case["code"]), case["null_code"], case["imperial"], case["pressure"], case["maximum"], case["added"], output, len(output))
        self.assertGreater(size, 0)
        result = json.loads(output.value)
        for name in ("code_storage", "string_storage"):
            result[name + "_sha256"] = hashlib.sha256(bytes.fromhex(result.pop(name))).hexdigest()
        return result

    def test_original_repository_buffer_writer_and_statistics(self):
        self.assertEqual(len(self.cases), len(self.fixture["cases"]))
        for index, (case, expected) in enumerate(zip(self.cases, self.fixture["cases"])):
            with self.subTest(index=index, group=case["group"], action=case["action"]):
                self.assertEqual(self.actual(case), expected)

    def test_all_builtin_programs_and_buffer_use_actual_runtime(self):
        self.assertEqual(self.native.runtime_integration(), 0)

    def test_typed_catalog_matches_preserved_image(self):
        sys.path.insert(0, str(ROOT / "tools"))
        try:
            import recover_programs
            for name, content in recover_programs.outputs().items(): self.assertEqual((ROOT / name).read_text(), content, name)
        finally:
            sys.path.remove(str(ROOT / "tools"))

    def test_fixture_provenance(self):
        if self.fixture["mode"] == "smoke": self.skipTest("Development smoke is not committed evidence")
        fixture = ROOT / "tests/fixtures/program-repository-original.json"
        report = json.loads((ROOT / "analysis/program-repository-validation.json").read_text())
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(), report["fixture_sha256"])
        self.assertEqual(len(self.cases), report["cases"])
        raw = common.INPUT.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), report["input_sha256"])
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                    for s in common.images.parse_image(raw)["segments"]]
        for name, expected in {**report["generator_files"], **report["validated_source_sha256"]}.items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected, name)
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])


if __name__ == "__main__":
    unittest.main()
