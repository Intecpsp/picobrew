import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_local_http_state as validation


class LocalHttpStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='z-http-state-test-')
        cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads((ROOT/'tests/fixtures/local-http-state-original.json').read_text())

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    def test_original_complete_state_json_bytes(self):
        self.assertEqual(self.fixture['input_sha256'],validation.IMAGE_SHA)
        self.assertEqual([x['input'] for x in self.fixture['cases']],validation.input_cases())
        for case in self.fixture['cases']:
            with self.subTest(case=case['input']['id']):
                self.assertEqual(validation.native_case(self.native,case['input']),case['original'])

    def test_evidence_and_pinned_target_source_identity(self):
        report=json.loads((ROOT/'analysis/local-http-state-validation.json').read_text())
        self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/local-http-state-original.json'))
        for path,expected in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),expected,path)
        target=json.loads((ROOT/'analysis/local-http-state-target-build.json').read_text())
        self.assertFalse(target['hardware_tested']);self.assertFalse(target['complete_application_linked'])
        for item in target['sources']:self.assertEqual(validation.digest(ROOT/item['source']),item['source_sha256'])
        for path,expected in target['owned_header_sha256'].items():self.assertEqual(validation.digest(ROOT/path),expected)

    def test_concrete_control_snapshot_bounds_and_output_failure(self):
        source=Path(self.build.name)/'state_bindings.cpp';binary=Path(self.build.name)/'state_bindings'
        source.write_text(r'''
#include "zseries/local_http_state.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
static unsigned writes=0;
static bool failed_headers(void*,std::uint32_t,std::int32_t,const char*){return false;}
static bool write(void*,const std::uint8_t*,std::size_t){++writes;return true;}
int main(){
    HeaterSensors sensors;for(unsigned i=0;i<4;++i)sensors.temperatures[i].celsius=20+i;
    HeaterController heater;heater.state.target_celsius=64.5;
    z_msp_state msp{};msp.valve_position=6;msp.pumps=0x10;
    ProgramRuntime runtime;runtime.state.pause_reason=2;runtime.state.step_name="Mash";
    runtime.state.elapsed_seconds=11;runtime.state.total_left_seconds=22;runtime.state.flags=1;
    const auto state=local_http_state(sensors,heater,msp,runtime,-73);
    char output[2048];std::size_t size=999;
    assert(serialize_local_http_state(state,output,sizeof output,size));
    assert(std::strstr(output,"\"ThermoBlockTemp\":20,\"WortTemp\":21,\"AmbientTemp\":23,\"DrainTemp\":22"));
    assert(std::strstr(output,"\"rssi\":-73,\"TargetTemp\":64.5,\"StepName\":\"Mash\""));
    assert(std::strstr(output,"\"SecondsEllapsed\":11,\"SecondsRemaining\":22,\"UsePicoPakMode\":1,\"Location\":3"));
    assert(size==std::strlen(output));
    char tiny[2]={'x','y'};assert(serialize_local_http_state(state,tiny,1,size));assert(size==0&&tiny[0]==0&&tiny[1]=='y');
    size=123;assert(!serialize_local_http_state(state,nullptr,5,size));assert(size==123);
    auto invalid=state;invalid.pause_reason=18;assert(!serialize_local_http_state(invalid,output,sizeof output,size));
    LocalHttpServer server;assert(send_local_http_state(server,state)==LocalHttpResult::MissingService);
    server.services.response.headers=failed_headers;server.services.response.write=write;
    assert(send_local_http_state(server,state)==LocalHttpResult::ServiceFailure);assert(writes==0);
    assert(server.frozen&&server.length==0);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'include'),str(source),
            str(ROOT/'src/local_http_state.cpp'),str(ROOT/'src/local_http_server.cpp'),str(ROOT/'src/local_http.cpp'),
            str(ROOT/'src/http_assets.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=5)


if __name__=='__main__':unittest.main()
