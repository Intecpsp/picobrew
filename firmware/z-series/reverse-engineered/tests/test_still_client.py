import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_still_client as validation

class StillClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='z-still-client-test-');cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/still-client-original.json.gz').read_bytes()))
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def test_original_protocol_sensor_parser_and_statistics(self):
        self.assertEqual(self.fixture['input_sha256'],validation.IMAGE_SHA)
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):self.assertEqual(validation.native_case(self.native,row['input']),row['original'])
    def test_evidence_identity(self):
        report=json.loads((ROOT/'analysis/still-client-validation.json').read_text());self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(validation.input_cases()));self.assertEqual(report['golden_cases'],len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/still-client-original.json.gz'))
        self.assertEqual(report['input_sha256'],validation.digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin'))
        for path,expected in report['source_sha256'].items():
            with self.subTest(path=path):self.assertEqual(validation.digest(ROOT/path),expected)
    def test_fragmented_responses_controller_binding_and_undefined_input_repair(self):
        source=Path(self.build.name)/'still_client_integration.cpp';binary=Path(self.build.name)/'still_client_integration'
        source.write_text(r'''
#include "zseries/still_client.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
using namespace zseries::oem;
struct Fixture {
    EepromData settings;PicoStillController controller;PicoStillSamples samples;StillClient client;
    std::vector<StillHttpEffect> effects;std::string uri,form;const char* next_body=nullptr;std::uint32_t now=0;
    Fixture():controller(settings),client(controller.state,samples){}
};
static bool http(void* p,const StillHttpAction& a,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);f.effects.push_back(a.effect);result=1;
    if(a.effect==StillHttpEffect::Configure){assert(a.client_class==2);f.uri=a.uri;}
    if(a.effect==StillHttpEffect::Post){assert(std::strcmp(a.content_type,"application/x-www-form-urlencoded")==0);f.form.assign(a.body,a.body_length);}
    return true;
}
static bool controller(void* p,const PicoStillAction& a,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);result=1;
    switch(a.effect){
    case PicoStillEffect::HttpTick:
        if(f.next_body){StillHttpResponse headers;headers.status=200;f.client.receive_headers(headers);const auto n=std::strlen(f.next_body);
            assert(f.client.receive_body(f.next_body,n/2)==StillClientResult::Ok);assert(f.client.receive_body(f.next_body+n/2,n-n/2)==StillClientResult::Ok);f.client.complete(a.argument);f.next_body=nullptr;}break;
    case PicoStillEffect::ClientTick:f.client.tick();break;
    case PicoStillEffect::AuthorizationTick:case PicoStillEffect::EmitEvent:case PicoStillEffect::ReleaseRuntime:break;
    case PicoStillEffect::ClientCommand:if(f.client.command(static_cast<Command>(a.argument))!=StillClientResult::Ok)return false;break;
    case PicoStillEffect::ClientClear:if(f.client.reset()!=StillClientResult::Ok)return false;break;
    default:return false;
    }f.client.observe(f.controller.observed);return true;
}
int main(){
    Fixture f;auto& c=f.client;auto& controller_state=f.controller;
    assert(c.reset()==StillClientResult::MissingService);c.services.context=&f;c.services.perform=http;c.set_host("192.168.4.1");
    WifiCredentials credentials;std::strcpy(credentials.ssid.data(),"My Network");std::strcpy(credentials.password.data(),"p@ss/word!");
    assert(c.configure(credentials)==StillClientResult::Ok);assert(f.uri=="http://192.168.4.1/configure"&&f.form=="ssid=My%20Network&pass=p%40ss%2Fword%21");
    std::strcpy(credentials.ssid.data(),"++++++++++++++++++++++");assert(c.configure(credentials)==StillClientResult::InvalidCredentialsEncoding);
    char encoded[8];std::memset(encoded,'Z',sizeof encoded);assert(still_form_encode(encoded,4," ")==-2);assert(encoded[0]=='Z');
    assert(still_form_encode(encoded,5," ")==0&&std::strcmp(encoded,"%20")==0);
    assert(c.receive_body(nullptr,1)==StillClientResult::InvalidBody);assert(c.receive_body(nullptr,0)==StillClientResult::Ok);
    c.reset();controller_state.services.context=&f;controller_state.services.perform=controller;controller_state.state.id=PicoStillStateId::PendingStart;
    PicoStillCommand start;start.id=StillCommandId::Start;assert(controller_state.commands.put(start));
    assert(controller_state.tick(100)==PicoStillResult::Ok&&controller_state.state.id==PicoStillStateId::Starting&&f.uri=="http://192.168.4.1/start");
    f.next_body="#OK#";assert(controller_state.tick(101)==PicoStillResult::Ok&&controller_state.state.id==PicoStillStateId::Running);
    assert(controller_state.tick(102)==PicoStillResult::Ok&&c.pending==Command::data&&f.uri=="http://192.168.4.1/data");
    f.next_body="#32.0,68.0,212.0,50.0,14.7,1,0,1,None#";assert(controller_state.tick(103)==PicoStillResult::Ok&&c.pending==Command::Empty);
    assert(f.samples.current.timestamp==103&&controller_state.state.sample_date==103&&controller_state.state.error[0]==0);
    assert(f.samples.current.temperature[0]==0&&f.samples.current.temperature[1]==20&&f.samples.current.temperature[2]==100&&f.samples.current.temperature[3]==10);
    assert(f.samples.current.pump_running&&!f.samples.current.fan_running&&f.samples.current.program_running);
    assert(f.samples.seconds10.timestamp==103&&f.samples.seconds60.timestamp==103&&f.samples.t1.count==1);
    // Public parser spans need not carry a readable terminator beyond their size.
    const char record[]="#32,68,212,50,14.7,1,0,1,None#";
    std::array<char,sizeof(record)-1> raw;std::memcpy(raw.data(),record,raw.size());PicoStillSamples direct;
    for(std::size_t n=0;n<raw.size();++n)assert(!direct.parse(200,raw.data(),n));
    assert(direct.parse(200,raw.data(),raw.size())&&direct.current.temperature[1]==20);
    // The OEM parser failure does not itself change StillClient's HTTP success.
    assert(c.command(Command::data)==StillClientResult::Ok);StillHttpResponse response;response.status=200;c.receive_headers(response);c.receive_body("#bad#",5);c.complete(200);
    assert(c.succeeded()&&f.samples.current.timestamp==0&&f.samples.current.temperature[1]==20&&c.assertion_line==0x14c);
    // Valid payloads still update IP/version on non-200 status; 404 remains retryable.
    c.pending=Command::stillip;response.status=404;c.receive_headers(response);c.receive_body("#192.168.1.73#",14);c.complete(201);
    assert(c.error&&c.retryable&&std::strcmp(controller_state.state.ip.data(),"192.168.1.73")==0);
    c.pending=Command::version;response.status=200;c.receive_headers(response);const std::string long_version="#"+std::string(40,'v')+"#";c.receive_body(long_version.data(),long_version.size());c.complete(202);
    assert(std::strlen(controller_state.state.version.data())==31);
    c.reset();c.observe(controller_state.observed);PicoStillCommand stop;stop.id=StillCommandId::Stop;assert(controller_state.commands.put(stop));
    assert(controller_state.tick(203)==PicoStillResult::Ok&&controller_state.state.id==PicoStillStateId::Stopping&&f.uri=="http://192.168.4.1/end");
    f.next_body="#OK#";assert(controller_state.tick(204)==PicoStillResult::Ok&&controller_state.state.id==PicoStillStateId::Idle);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-fno-fast-math','-ffp-contract=off','-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I',str(ROOT/'include'),str(source),str(ROOT/'src/still_client.cpp'),str(ROOT/'src/pico_still_sensors.cpp'),str(ROOT/'src/pico_still.cpp'),str(ROOT/'src/enums.cpp'),str(ROOT/'src/eeprom.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
