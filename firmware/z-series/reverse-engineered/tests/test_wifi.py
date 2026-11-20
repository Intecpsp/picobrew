import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_wifi as validation

class WifiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-wifi-test-')
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / 'tests/fixtures/wifi-original.json').read_text())

    @classmethod
    def tearDownClass(cls): cls.build.cleanup()

    def test_original_state_machine_backoff_and_network_calls(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):
                self.assertEqual(validation.native_case(self.native, item['input']), item['original'])

    def test_evidence_identity(self):
        report = json.loads((ROOT / 'analysis/wifi-validation.json').read_text())
        self.assertTrue(report['all_results_match']); self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'], len(validation.input_cases()))
        self.assertEqual(report['golden_cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/wifi-original.json'))
        self.assertEqual(report['input_sha256'], validation.digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin'))
        for path, expected in report['source_sha256'].items():
            with self.subTest(path=path): self.assertEqual(validation.digest(ROOT / path), expected)

    def test_missing_services_and_multi_tick_core_mailbox_integration(self):
        source = Path(self.build.name) / 'wifi_integration.cpp'; binary = Path(self.build.name) / 'wifi_integration'
        source.write_text(r'''
#include "zseries/wifi.hpp"
#include "zseries/core_flow.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
struct Fixture {EepromData settings;CoreFlow core;std::uint32_t now=0,status=0,scans=0xffffffffu;Fixture():core(settings){}};
static std::uint32_t clock(void* p){return static_cast<Fixture*>(p)->now;}
static bool failed(void*,const WifiAction&,std::uint32_t&){return false;}
static bool perform(void* p,const WifiAction& a,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);result=1;
    switch(a.effect){
    case WifiEffect::Status:result=f.status;break;
    case WifiEffect::ScanComplete:result=f.scans;break;
    case WifiEffect::LocalAddress:result=0x0201a8c0;break;
    case WifiEffect::GatewayAddress:result=0x0101a8c0;break;
    case WifiEffect::Rssi:result=static_cast<std::uint32_t>(-60);break;
    case WifiEffect::ResolveHost:*a.address=0x08080808;break;
    case WifiEffect::EmitEvent:{CoreWifiEvent event;event.type=static_cast<WifiEventType>(a.argument);event.argument=a.second;event.second_argument=a.third;f.core.wifi_events.put(event);break;}
    default:break;
    }return true;
}
int main(){
    WifiManager missing;
    assert(missing.tick(0)==WifiResult::MissingService);
    assert(missing.transition(WiFiStateId::Idle)==WifiResult::MissingService);
    assert(missing.state.id==WiFiStateId::Booting);
    assert(missing.setup()==WifiResult::MissingService);
    missing.services.millis=clock;missing.services.perform=failed;
    assert(missing.tick(0)==WifiResult::ServiceFailure);
    Fixture f;WifiManager wifi;wifi.services.context=&f;wifi.services.millis=clock;wifi.services.perform=perform;
    WifiCredentials c;std::strcpy(c.ssid.data(),"fixture-network");std::strcpy(c.password.data(),"fixture-password");wifi.connect(c);
    auto tick=[&](std::uint32_t now){f.now=now;assert(wifi.tick(now)==WifiResult::Ok);};
    tick(0);assert(wifi.state.id==WiFiStateId::Idle);
    tick(100);assert(wifi.state.id==WiFiStateId::PoweringUp);
    tick(600);assert(wifi.state.id==WiFiStateId::PoweringUp);
    tick(601);assert(wifi.state.id==WiFiStateId::Scanning);
    tick(602);assert(wifi.state.id==WiFiStateId::Scanning);
    f.scans=2;tick(603);assert(wifi.state.id==WiFiStateId::ScanCompleted);
    CoreWifiEvent event;assert(f.core.wifi_events.take(event));assert(event.type==WifiEventType::ScanStarted);
    tick(604);assert(wifi.state.id==WiFiStateId::Connecting);
    f.status=3;tick(605);assert(wifi.state.id==WiFiStateId::Checking);
    tick(606);assert(wifi.state.id==WiFiStateId::Connected);assert(wifi.state.connected.equals(c));
    assert(f.core.wifi_events.take(event));assert(event.type==WifiEventType::Connected);
    f.status=6;tick(607);assert(wifi.state.id==WiFiStateId::Disconnecting);assert(wifi.state.rssi==0);
    tick(1607);assert(wifi.state.id==WiFiStateId::Disconnecting);
    tick(1608);assert(wifi.state.id==WiFiStateId::Idle);
    wifi.state.id=WiFiStateId::Connecting;f.status=1;tick(2000);
    assert(wifi.backoff.active&&wifi.backoff.deadline==62000&&wifi.backoff.duration==120000);
    tick(61999);assert(wifi.state.id==WiFiStateId::Idle);
    tick(62000);assert(wifi.state.id==WiFiStateId::PoweringUp);
    wifi.state.id=WiFiStateId::Connecting;tick(62001);
    assert(wifi.backoff.attempts==2&&wifi.backoff.deadline==182001&&wifi.backoff.duration==240000);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        '-I',str(ROOT/'include'),str(source),str(ROOT/'src/wifi.cpp'),str(ROOT/'src/eeprom.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
