import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_local_http_server as validation


class LocalHttpServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-http-server-test-')
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / 'tests/fixtures/local-http-server-original.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_connection_framing_and_header_bytes(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        self.assertEqual([item['input'] for item in self.fixture['cases']], validation.input_cases())
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):
                self.assertEqual(validation.native_case(self.native, item['input']), item['original'])

    def test_evidence_identifies_current_source(self):
        report = json.loads((ROOT / 'analysis/local-http-server-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],
                         validation.digest(ROOT / 'tests/fixtures/local-http-server-original.json'))
        for path, expected in report['source_sha256'].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        target = json.loads((ROOT / 'analysis/local-http-server-target-build.json').read_text())
        self.assertFalse(target['complete_application_linked'])
        self.assertFalse(target['hardware_tested'])
        for entry in target['sources']:
            self.assertEqual(validation.digest(ROOT / entry['source']), entry['source_sha256'])
        for path, expected in target['owned_header_sha256'].items():
            self.assertEqual(validation.digest(ROOT / path), expected)

    def test_service_failures_close_completed_request_and_stop_header_output(self):
        source = Path(self.build.name) / 'http_server_guards.cpp'
        binary = Path(self.build.name) / 'http_server_guards'
        source.write_text(r'''
#include "zseries/local_http_server.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
struct Calls { unsigned closes=0,writes=0; };
static bool yes(void*){return true;}
static int empty(void*){return 0;}
static bool close(void* c){++static_cast<Calls*>(c)->closes;return true;}
static bool fail_write(void* c,const std::uint8_t*,std::size_t){++static_cast<Calls*>(c)->writes;return false;}
int main(){
    Calls calls;LocalHttpServer server;
    assert(server.tick(10)==LocalHttpResult::Ok);
    assert(server.enable(true)==LocalHttpResult::MissingService);assert(!server.enabled);
    server.enabled=true;
    assert(server.tick(10)==LocalHttpResult::MissingService);
    auto& io=server.services.transport;io.context=&calls;io.connected=yes;io.accept=yes;
    io.available=empty;io.read=empty;io.close=close;
    server.deadline=10;server.request_complete=true;
    server.parser.parse_request_line("GET /ping HTTP/1.0");
    assert(server.tick(10)==LocalHttpResult::MissingService);assert(calls.closes==1);
    server.parser.parse_request_line("POST /recipe HTTP/1.0");
    assert(server.tick(10)==LocalHttpResult::MissingService);assert(calls.closes==2);
    assert(write_local_http_headers(io,200,5,nullptr)==LocalHttpResult::MissingService);
    io.write=fail_write;
    assert(write_local_http_headers(io,200,5,nullptr)==LocalHttpResult::ServiceFailure);
    assert(calls.writes==1);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'include'),str(source),
                        str(ROOT/'src/local_http_server.cpp'),str(ROOT/'src/local_http.cpp'),
                        str(ROOT/'src/http_assets.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=5)


if __name__ == '__main__':
    unittest.main()
