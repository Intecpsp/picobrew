import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_http_client as validation

class HttpClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='z-http-client-test-');cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/http-client-original.json.gz').read_bytes()))
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def test_original_uri_request_and_response_machine(self):
        self.assertEqual(self.fixture['input_sha256'],validation.IMAGE_SHA)
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):self.assertEqual(validation.native_case(self.native,row['input']),row['original'])
    def test_evidence_identity(self):
        report=json.loads((ROOT/'analysis/http-client-validation.json').read_text());self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(validation.input_cases()));self.assertEqual(report['golden_cases'],len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/http-client-original.json.gz'))
        self.assertEqual(report['input_sha256'],validation.digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin'))
        for path,expected in report['source_sha256'].items():
            with self.subTest(path=path):self.assertEqual(validation.digest(ROOT/path),expected)
    def test_fragmented_transport_through_still_client_and_controller(self):
        source=Path(self.build.name)/'http_client_integration.cpp';binary=Path(self.build.name)/'http_client_integration'
        source.write_text(r'''
#include "zseries/still_http.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
using namespace zseries::oem;
struct Fixture {
    EepromData settings;PicoStillController controller;PicoStillSamples samples;StillClient still;HttpClient http;StillHttpBinding binding;
    std::string rx,tx;std::size_t position=0;std::uint32_t now=0;bool connected=true;unsigned stops=0,headers=0,done=0;
    Fixture():controller(settings),still(controller.state,samples),binding(http,still){}
};
static std::uint32_t clock(void* p){return ++static_cast<Fixture*>(p)->now;}
static bool socket(void* p,const HttpAction& a,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);result=0;
    switch(a.effect){
    case HttpEffect::ConnectHost:case HttpEffect::ConnectAddress:result=1;f.tx.clear();f.rx.clear();f.position=0;f.connected=true;break;
    case HttpEffect::Write:f.tx.append(reinterpret_cast<const char*>(a.data),a.size);result=a.size;break;
    case HttpEffect::Available:result=f.rx.size()-f.position;break;
    case HttpEffect::ReadByte:assert(f.position<f.rx.size());result=static_cast<unsigned char>(f.rx[f.position++]);break;
    case HttpEffect::ReadBytes:{const auto n=std::min(a.size,f.rx.size()-f.position);std::memcpy(a.destination,f.rx.data()+f.position,n);f.position+=n;result=n;break;}
    case HttpEffect::Connected:result=f.connected;break;
    case HttpEffect::Stop:++f.stops;f.connected=false;break;
    case HttpEffect::FreeHeap:result=100000;break;
    case HttpEffect::Flush:case HttpEffect::SocketError:break;
    default:return false;
    }return true;
}
static bool controller(void* p,const PicoStillAction& a,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);result=1;
    switch(a.effect){
    case PicoStillEffect::HttpTick:if(tick_still_http(f.binding,a.argument)!=HttpResult::Ok)return false;break;
    case PicoStillEffect::ClientTick:f.still.tick();break;
    case PicoStillEffect::ClientCommand:if(f.still.command(static_cast<Command>(a.argument))!=StillClientResult::Ok)return false;break;
    case PicoStillEffect::ClientClear:if(f.still.reset()!=StillClientResult::Ok)return false;break;
    case PicoStillEffect::AuthorizationTick:case PicoStillEffect::EmitEvent:case PicoStillEffect::ReleaseRuntime:break;
    default:return false;
    }f.still.observe(f.controller.observed);return true;
}
static void tick(Fixture& f){++f.now;assert(f.controller.tick(f.now)==PicoStillResult::Ok);}
static bool headers(void* p,const HttpResponse&){++static_cast<Fixture*>(p)->headers;return true;}
static void body(void*,const std::uint8_t*,std::size_t){}
static void complete(void* p){++static_cast<Fixture*>(p)->done;}
int main(){
    Fixture f;f.http.services.context=&f;f.http.services.millis=clock;f.http.services.perform=socket;f.still.services=still_http_services(f.binding);
    f.still.set_host("192.168.4.1");f.controller.services.context=&f;f.controller.services.perform=controller;f.controller.state.id=PicoStillStateId::PendingStart;
    PicoStillCommand start;start.id=StillCommandId::Start;assert(f.controller.commands.put(start));tick(f);
    assert(f.controller.state.id==PicoStillStateId::Starting&&f.http.state==HttpState::WaitingReply);
    assert(f.tx=="GET /start HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 0\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    f.rx="HTTP/1.1 200 OK\r\nContent-Len";tick(f);tick(f);assert(f.http.state==HttpState::Headers&&f.http.stalled);
    f.rx+="gth: 4\r\n\r\n#O";tick(f);assert(f.http.state==HttpState::Body);tick(f);assert(f.still.body_length==2);
    f.rx+="K#";tick(f);assert(f.http.state==HttpState::Complete);tick(f);assert(f.controller.state.id==PicoStillStateId::Running&&f.http.state==HttpState::Idle);
    tick(f);assert(f.still.pending==Command::data&&f.tx.find("GET /data HTTP/1.1\r\n")==0);
    const std::string record="#32,68,212,50,14.7,1,0,1,None#";
    f.rx="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(record.size())+"\r\n\r\n"+record;
    tick(f);tick(f);tick(f);tick(f);
    assert(f.samples.current.temperature[0]==0&&f.samples.current.temperature[1]==20&&f.samples.current.temperature[2]==100&&f.samples.current.temperature[3]==10);
    assert(f.samples.current.pump_running&&!f.samples.current.fan_running&&f.samples.current.program_running&&f.samples.current.timestamp!=0);
    assert(f.still.pending==Command::Empty&&f.controller.state.sample_date==f.samples.current.timestamp);
    PicoStillCommand stop;stop.id=StillCommandId::Stop;assert(f.controller.commands.put(stop));tick(f);
    assert(f.controller.state.id==PicoStillStateId::Stopping&&f.tx.find("GET /end HTTP/1.1\r\n")==0);
    f.rx="HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n#OK#";tick(f);tick(f);tick(f);tick(f);
    assert(f.controller.state.id==PicoStillStateId::Idle&&f.stops==3);
    // Transport errors arrive before StillClient's following tick. A 404 is
    // rejected by HttpClient before the protocol's response callback sees it.
    assert(f.still.command(Command::version)==StillClientResult::Ok);f.rx="HTTP/1.1 404 Not Found\r\n\r\n";
    assert(tick_still_http(f.binding,++f.now)==HttpResult::Ok);assert(tick_still_http(f.binding,++f.now)==HttpResult::Ok);f.still.tick();
    assert(f.still.error&&!f.still.retryable&&!f.still.response_done&&f.http.error==HttpClientErrorId::HttpStatusError);
    f.http.reset();HttpReceiver receiver;receiver.context=&f;receiver.headers=headers;receiver.body=body;receiver.complete=complete;bool accepted=false;
    assert(f.http.configure("http://host",receiver,HttpClientClass::Wifi,accepted)==HttpResult::Ok&&accepted);
    assert(f.http.get(accepted)==HttpResult::Ok&&accepted&&f.tx.find("GET  HTTP/1.1\r\n")==0);
    f.rx="HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nA";
    assert(f.http.tick(++f.now)==HttpResult::Ok);assert(f.http.tick(++f.now)==HttpResult::Ok);assert(f.http.tick(++f.now)==HttpResult::Ok);
    assert(f.http.state==HttpState::Body&&f.http.content_received==1&&f.headers==1);
    f.now+=100000;assert(f.http.tick(f.now)==HttpResult::Ok&&f.http.state==HttpState::Body); // No OEM body timeout.
    f.connected=false;assert(f.http.tick(++f.now)==HttpResult::Ok&&f.http.state==HttpState::Complete);
    assert(f.http.tick(++f.now)==HttpResult::Ok&&f.done==1); // Early body close still completes.
    f.http.reset();assert(f.http.configure("http://host/p",receiver,HttpClientClass::Wifi,accepted)==HttpResult::Ok&&accepted);
    assert(f.http.get(accepted)==HttpResult::Ok&&accepted);const auto deadline=f.http.deadline;
    assert(f.http.tick(deadline)==HttpResult::Ok&&f.http.state==HttpState::WaitingReply);
    assert(f.http.tick(deadline+1)==HttpResult::Ok&&f.http.error==HttpClientErrorId::WaitingReplyTimeout);
    HttpRequest request;const char malformed[]="http://host/path:bad";assert(!request.parse(malformed)&&!request.valid());
    HttpClient unbound;assert(unbound.get(accepted)==HttpResult::MissingService);
    unbound.services.millis=clock;unbound.services.context=&f;
    assert(unbound.configure("http://host/",receiver,HttpClientClass::None,accepted)==HttpResult::MissingService&&!accepted);
}
''')
        sources=['src/http_client.cpp','src/still_http.cpp','src/still_client.cpp','src/pico_still_sensors.cpp','src/pico_still.cpp','src/enums.cpp','src/eeprom.cpp']
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-fno-fast-math','-ffp-contract=off','-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I',str(ROOT/'include'),str(source)]+[str(ROOT/p) for p in sources]+['-o',str(binary)],check=True)
        environment=os.environ.copy();environment.setdefault('ASAN_OPTIONS','detect_leaks=0')
        subprocess.run([str(binary)],check=True,env=environment)
if __name__=='__main__':unittest.main()
