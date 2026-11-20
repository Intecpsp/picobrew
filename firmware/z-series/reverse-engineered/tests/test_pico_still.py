import json
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_pico_still as validation

class PicoStillTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='z-pico-still-test-')
        cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/pico-still-original.json.gz').read_bytes()))
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def test_original_controller_calls_state_and_event_order(self):
        self.assertEqual(self.fixture['input_sha256'],validation.IMAGE_SHA)
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):self.assertEqual(validation.native_case(self.native,row['input']),row['original'])
    def test_evidence_identity(self):
        report=json.loads((ROOT/'analysis/pico-still-validation.json').read_text())
        self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(validation.input_cases()));self.assertEqual(report['golden_cases'],len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/pico-still-original.json.gz'))
        self.assertEqual(report['input_sha256'],validation.digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin'))
        for path,expected in report['source_sha256'].items():
            with self.subTest(path=path):self.assertEqual(validation.digest(ROOT/path),expected)
    def test_network_authorization_running_and_stop_integration(self):
        source=Path(self.build.name)/'pico_still_integration.cpp';binary=Path(self.build.name)/'pico_still_integration'
        source.write_text(r'''
#include "zseries/pico_still.hpp"
#include <cassert>
#include <cstring>
#include <vector>
using namespace zseries::oem;
struct Fixture {
    EepromData settings;CoreFlow core;WifiManager wifi;PicoStillController still;
    std::vector<PicoStillEffect> trace;unsigned releases=0;
    Fixture():core(settings),still(settings){std::strcpy(settings.wifi_ssid.data(),"home");std::strcpy(settings.wifi_password.data(),"password");}
};
static bool failed(void*,const PicoStillAction&,std::uint32_t&){return false;}
static bool service(void* p,const PicoStillAction& a,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);auto& o=f.still.observed;result=1;f.trace.push_back(a.effect);
    switch(a.effect){
    case PicoStillEffect::WifiTarget:f.wifi.select_target(static_cast<WiFiStateId>(a.argument));break;
    case PicoStillEffect::WifiConnect:f.wifi.connect(*a.credentials);break;
    case PicoStillEffect::ReleaseRuntime:++f.releases;break;
    case PicoStillEffect::ScanCount:result=2;break;
    case PicoStillEffect::ScanSsid:std::strcpy(a.ssid->data(),a.argument==0?"home":"PicoStill_ABC123");break;
    case PicoStillEffect::AuthorizationRequest:o.authorization_pending=true;break;
    case PicoStillEffect::ClientCommand:case PicoStillEffect::ClientConfigure:
        o.pending=a.effect==PicoStillEffect::ClientConfigure?Command::configure:static_cast<Command>(a.argument);o.client_done=o.client_error=o.client_retryable=false;break;
    case PicoStillEffect::ClientClear:o.pending=Command::Empty;o.client_done=o.client_error=o.client_retryable=false;break;
    case PicoStillEffect::EmitEvent:{CoreStillEvent event;event.type=StillEventId::StateTransition;event.state=static_cast<PicoStillStateId>(a.argument);f.core.still_events.put(event);break;}
    default:break;
    }return true;
}
int main(){
    Fixture f;auto& c=f.still;
    assert(c.setup()==PicoStillResult::MissingService);c.services.perform=failed;assert(c.setup()==PicoStillResult::ServiceFailure);
    c.services.context=&f;c.services.perform=service;assert(c.setup()==PicoStillResult::Ok);
    auto advance=[&](std::uint32_t now){assert(c.advance(now)==PicoStillResult::Ok);};
    auto post=[&](StillCommandId id){PicoStillCommand command;command.id=id;command.selection=0;assert(c.commands.put(command));};
    auto wifi=[&](WifiEventType type){CoreWifiEvent event;event.type=type;assert(c.wifi_events.put(event));advance(100);};
    auto done=[&](){c.observed.client_done=true;advance(100);};
    post(StillCommandId::Scan);advance(100);assert(c.state.id==PicoStillStateId::Scanning&&f.wifi.target==WiFiStateId::Scanning);
    CoreStillEvent event;assert(f.core.still_events.take(event)&&event.state==PicoStillStateId::Scanning);
    wifi(WifiEventType::ScanCompleted);assert(c.state.available_count==1&&std::strcmp(c.state.available[0].data(),"ABC123")==0);
    post(StillCommandId::Connect);advance(100);assert(c.state.id==PicoStillStateId::ConnectingToHomeAP1&&std::strcmp(f.wifi.state.requested.ssid.data(),"home")==0);
    wifi(WifiEventType::Connected);assert(c.state.id==PicoStillStateId::Authorizing&&c.observed.authorization_pending);
    c.observed.authorization_pending=false;c.observed.authorized=true;c.observed.cleaned=false;advance(100);assert(c.state.id==PicoStillStateId::PendingCleanedAck);
    post(StillCommandId::CleanedAck);advance(100);assert(c.state.id==PicoStillStateId::SendingCleanedAck);
    c.observed.authorization_pending=false;c.observed.cleaned=true;advance(100);
    assert(c.state.id==PicoStillStateId::ConnectingToStillAP&&!f.wifi.state.requested.internet_expected);
    assert(std::strcmp(f.wifi.state.requested.ssid.data(),"PicoStill_ABC123")==0);
    wifi(WifiEventType::Connected);assert(c.observed.pending==Command::version);done();assert(c.observed.pending==Command::stillip);
    std::strcpy(c.state.ip.data(),"0.0.0.0");done();assert(c.state.id==PicoStillStateId::Configuring&&c.observed.pending==Command::configure);
    done();wifi(WifiEventType::Connected);done();std::strcpy(c.state.ip.data(),"192.168.1.73");done();assert(c.observed.pending==Command::connecttolan);
    done();assert(c.state.id==PicoStillStateId::ConnectingToHomeAP2&&f.wifi.state.requested.internet_expected);
    wifi(WifiEventType::Connected);done();assert(c.state.id==PicoStillStateId::PendingStart&&f.releases==1);
    post(StillCommandId::Start);advance(100);assert(c.observed.pending==Command::start);done();assert(c.state.id==PicoStillStateId::Running);
    advance(0);assert(c.observed.pending==Command::Empty);advance(1);assert(c.observed.pending==Command::data);
    for(unsigned n=1;n<=5;++n){c.observed.client_error=c.observed.client_retryable=true;advance(n*3000);assert(c.state.consecutive_failures==static_cast<int>(n));assert(c.state.polling_stopped==(n==5));if(n<5)advance(n*3000+2001);}
    post(StillCommandId::Reconnect);advance(16000);assert(!c.state.polling_stopped&&c.next_poll_date==18000);
    advance(18000);assert(c.observed.pending==Command::Empty);advance(18001);assert(c.observed.pending==Command::data);
    done();post(StillCommandId::FanOn);advance(19000);assert(c.observed.pending==Command::fanon);done();
    post(StillCommandId::Stop);wifi(WifiEventType::Disconnecting);assert(c.commands.pending&&c.state.id==PicoStillStateId::Running);
    advance(20000);assert(c.state.id==PicoStillStateId::Stopping&&c.observed.pending==Command::end);
    c.state.polling_stopped=true;c.observed.client_error=true;advance(20001);assert(c.state.id==PicoStillStateId::Idle&&f.releases==2);
    c.state.id=PicoStillStateId::SelectingStill;PicoStillCommand bad;bad.id=StillCommandId::Connect;bad.selection=-1;assert(c.commands.put(bad));
    assert(c.advance(0)==PicoStillResult::InvalidSelection);assert(c.state.id==PicoStillStateId::SelectingStill);
    c.state.id=PicoStillStateId::Idle;f.trace.clear();assert(c.tick(30000)==PicoStillResult::Ok);
    assert(f.trace.size()==3&&f.trace[0]==PicoStillEffect::HttpTick&&f.trace[1]==PicoStillEffect::ClientTick&&f.trace[2]==PicoStillEffect::AuthorizationTick);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'include'),
            str(source),str(ROOT/'src/pico_still.cpp'),str(ROOT/'src/wifi.cpp'),str(ROOT/'src/eeprom.cpp'),str(ROOT/'src/enums.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
