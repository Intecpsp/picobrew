"""Original-instruction byte/state/event acceptance for the EEPROM reconstruction."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

import eeprom_cases as inputs
import run_msp_emulation as common

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = r'''
#include "zseries/eeprom.hpp"
#include <array>
#include <cstring>
#include <vector>
using namespace zseries::oem;
using Word=std::uint32_t;
struct Event { Word kind; Word value; EepromBytes bytes; };
struct Fixture {
    EepromBytes cache; Word actual; bool dirty,begin_result,commit_result;
    std::vector<Event> events;
    bool begin(Word requested) { events.push_back({1,requested,{}});return begin_result; }
    template<class T> T& get(int address,T& destination) {
        if(address<0 || static_cast<Word>(address)+sizeof(T)>actual)return destination;
        std::memcpy(&destination,cache.data()+address,sizeof(T));return destination;
    }
    template<class T> const T& put(int address,const T& source) {
        if(address<0 || static_cast<Word>(address)+sizeof(T)>actual)return source;
        std::memcpy(cache.data()+address,&source,sizeof(T));dirty=true;return source;
    }
    bool commit(){events.push_back({2,dirty,cache});if(commit_result)dirty=false;return commit_result;}
};
static void registration(void* p,EepromStore&,const std::array<EepromCommandDescriptor,9>&){
    static_cast<Fixture*>(p)->events.push_back({3,0,{}});
}
extern "C" void run_case(Word action,const std::uint8_t* data,const std::uint8_t* cache,
    const Word* args,const char* text,std::uint8_t* out_data,std::uint8_t* out_cache,
    Word* meta,Word* event_words,std::uint8_t* event_bytes){
    EepromBytes bytes;std::memcpy(bytes.data(),data,120);
    Fixture fixture{{},args[3],args[1]!=0,args[4]!=0,args[5]!=0,{}};
    std::memcpy(fixture.cache.data(),cache,120);
    EepromStore store;store.data=decode_eeprom(bytes);store.initialized=args[0]!=0;store.requested_bytes=args[2];
    store.backend=arduino_eeprom_backend(fixture);store.services.context=&fixture;store.services.register_commands=registration;
    Word value=0;EepromResult result=EepromResult::Ok;
    switch(action){
    case 0:store.data=EepromData{};break;
    case 1:value=eeprom_enforce_valid_enums(store.data);break;
    case 2:value=eeprom_data_valid(store.data);break;
    case 3:value=eeprom_text_boolean(text);break;
    case 4:value=eeprom_set_ssid(store.data,text);break;
    case 5:value=eeprom_set_password(store.data,text);break;
    case 6:result=store.setup();break;
    case 7:result=store.write();break;
    case 8:result=store.reset();break;
    case 9:{EepromData* pointer=nullptr;result=store.get_data(pointer);value=pointer==&store.data;break;}
    case 10:result=store.command(EepromCommand::Ssid,text);break;
    case 11:result=store.command(EepromCommand::Password,text);break;
    case 12:result=store.command(EepromCommand::Azure,text);break;
    case 13:result=store.command(EepromCommand::Ethernet,text);break;
    case 14:result=store.command(EepromCommand::BoilerUnknown);break;
    case 15:result=store.command(EepromCommand::BoilerBig);break;
    case 16:result=store.command(EepromCommand::BoilerSmall);break;
    }
    bytes=encode_eeprom(store.data);std::memcpy(out_data,bytes.data(),120);std::memcpy(out_cache,fixture.cache.data(),120);
    meta[0]=store.assertion_line;meta[1]=value;meta[2]=store.initialized;meta[3]=fixture.dirty;
    meta[4]=fixture.events.size();meta[5]=store.last_begin_succeeded;meta[6]=store.last_commit_succeeded;meta[7]=static_cast<Word>(result);
    for(std::size_t i=0;i<fixture.events.size();i++){
        event_words[2*i]=fixture.events[i].kind;event_words[2*i+1]=fixture.events[i].value;
        std::memcpy(event_bytes+120*i,fixture.events[i].bytes.data(),120);
    }
}
extern "C" int api_edges(){
    EepromStore store;
    if(store.setup()!=EepromResult::MissingBackend)return 1;
    if(store.write()!=EepromResult::NotInitialized || store.assertion_line!=0x160)return 2;
    if(store.reset()!=EepromResult::NotInitialized || store.assertion_line!=0x158)return 3;
    EepromData* p=&store.data;
    if(store.get_data(p)!=EepromResult::NotInitialized || p || store.assertion_line!=0x130)return 4;
    if(store.command(EepromCommand::Info)!=EepromResult::MissingReporter)return 5;
    if(store.command(static_cast<EepromCommand>(999))!=EepromResult::InvalidCommand)return 6;
    if(store.command(EepromCommand::Test)!=EepromResult::Ok || !eeprom_original_self_test())return 7;
    Fixture fixture{{},512,false,false,false,{}};store.backend=arduino_eeprom_backend(fixture);
    if(store.setup()!=EepromResult::MissingRegistry || !store.initialized || store.last_begin_succeeded)return 8;
    store.services.context=&fixture;store.services.register_commands=registration;
    if(store.setup()!=EepromResult::Ok || store.last_begin_succeeded)return 9;
    if(store.write()!=EepromResult::Ok || store.last_commit_succeeded || !fixture.dirty)return 10;
    static const char* tokens[]={"i","ssid ","pass ","azure ","eth ","boiler u","boiler b","boiler s","test"};
    const auto& commands=eeprom_commands();
    for(std::size_t i=0;i<commands.size();i++){
        if(std::strcmp(tokens[i],commands[i].token)!=0 || static_cast<std::size_t>(commands[i].id)!=i)return 11;
        if(commands[i].exact_match!=(i==0||i>=5))return 12;
    }
    unsigned reports=0;store.services.context=&reports;
    store.services.report_data=[](void* c,const EepromData&){++*static_cast<unsigned*>(c);};
    if(store.command(EepromCommand::Info)!=EepromResult::Ok || reports!=1)return 13;
    for(unsigned i=0;i<256;i++){
        EepromBytes bytes;for(std::size_t j=0;j<bytes.size();j++)bytes[j]=static_cast<std::uint8_t>(i+17*j);
        if(encode_eeprom(decode_eeprom(bytes))!=bytes)return 14;
    }
    return 0;
}
'''


class EepromTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-eeprom-native-")
        cls.addClassCleanup(cls.temporary.cleanup)
        build = Path(cls.temporary.name)
        bridge = build / "eeprom.cpp"
        bridge.write_text(BRIDGE)
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++11", "-O2", "-Wall", "-Wextra",
            "-Werror", "-pedantic", "-fPIC", "-shared", "-I", str(ROOT / "include"), str(bridge),
            str(ROOT / "src/eeprom.cpp"), "-o", str(build / "eeprom.so")], check=True, capture_output=True, text=True)
        cls.native = ctypes.CDLL(str(build / "eeprom.so"))
        word, byte = ctypes.c_uint32, ctypes.c_uint8
        cls.native.run_case.argtypes = [word, ctypes.POINTER(byte), ctypes.POINTER(byte), ctypes.POINTER(word),
            ctypes.c_char_p, ctypes.POINTER(byte), ctypes.POINTER(byte), ctypes.POINTER(word),
            ctypes.POINTER(word), ctypes.POINTER(byte)]
        cls.native.run_case.restype = None
        cls.native.api_edges.argtypes = []
        cls.native.api_edges.restype = ctypes.c_int
        fixture = Path(os.environ.get("Z_EEPROM_FIXTURE", ROOT / "tests/fixtures/eeprom-original.json"))
        cls.fixture = json.loads(fixture.read_text())
        cls.cases = inputs.cases()
        if cls.fixture["mode"] == "smoke":
            cls.cases = [c for i, c in enumerate(cls.cases) if c["action"] in (3,4,5,9,10,11,12,13,14,15,16) or i % 31 == 0]

    def actual(self, case):
        word, byte = ctypes.c_uint32, ctypes.c_uint8
        data = (byte * 120).from_buffer_copy(bytes.fromhex(case["data"]))
        cache = (byte * 120).from_buffer_copy(bytes.fromhex(case["cache"]))
        args = (word * 6)(*(case[k] for k in ("initialized", "dirty", "requested", "actual", "begin", "commit")))
        out_data, out_cache, meta, words, event_bytes = (byte * 120)(), (byte * 120)(), (word * 8)(), (word * 16)(), (byte * 960)()
        self.native.run_case(case["action"], data, cache, args, None if case["text"] is None else case["text"].encode(),
                             out_data, out_cache, meta, words, event_bytes)
        events = []
        for i in range(meta[4]):
            kind, value = words[2*i:2*i+2]
            if kind == 1: events.append(["begin", value])
            elif kind == 2: events.append(["commit", value, bytes(event_bytes[120*i:120*i+120]).hex()])
            elif kind == 3: events.append(["register"])
            else: self.fail(f"Unexpected event kind {kind}")
        return dict(assertion=meta[0], value=meta[1], data=bytes(out_data).hex(), cache=bytes(out_cache).hex(),
                    initialized=meta[2], dirty=meta[3], events=events)

    def test_data_validity_enums_strings_and_commands_match_original(self):
        self.assertEqual(len(self.cases), len(self.fixture["cases"]))
        for index, (case, expected) in enumerate(zip(self.cases, self.fixture["cases"])):
            if case["action"] in (6, 7, 8, 9): continue
            with self.subTest(index=index, group=case["group"], action=case["action"]):
                self.assertEqual(self.actual(case), expected)

    def test_setup_write_reset_getter_and_commit_order_match_original(self):
        for index, (case, expected) in enumerate(zip(self.cases, self.fixture["cases"])):
            if case["action"] not in (6, 7, 8, 9): continue
            with self.subTest(index=index, group=case["group"], action=case["action"]):
                self.assertEqual(self.actual(case), expected)

    def test_explicit_dependencies_sdk_adapter_and_round_trip(self):
        self.assertEqual(self.native.api_edges(), 0)

    def test_fixture_provenance(self):
        if self.fixture["mode"] == "smoke": self.skipTest("Development smoke is not committed evidence")
        fixture = ROOT / "tests/fixtures/eeprom-original.json"
        report = json.loads((ROOT / "analysis/eeprom-validation.json").read_text())
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
