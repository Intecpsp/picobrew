"""Exercise incoming HTTP behavior against independent original-instruction goldens."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import extract_http_assets
import validate_local_http as validation


class LocalHttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-local-http-test-')
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / 'tests/fixtures/local-http-original.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_parser_state_and_static_response_bytes(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        self.assertEqual([item['input'] for item in self.fixture['cases']], validation.input_cases())
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):
                self.assertEqual(validation.native_case(self.native, item['input']), item['original'])

    def test_static_assets_and_generated_source_match_preserved_image(self):
        for path, expected in extract_http_assets.outputs().items():
            with self.subTest(path=path):
                self.assertEqual((ROOT / path).read_bytes(), expected)

    def test_recorded_validation_and_target_build_identify_this_source(self):
        report = json.loads((ROOT / 'analysis/local-http-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['input_sha256'], validation.IMAGE_SHA)
        self.assertEqual(report['cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],
                         validation.digest(ROOT / 'tests/fixtures/local-http-original.json'))
        for path, expected in report['source_sha256'].items():
            with self.subTest(path=path):
                self.assertEqual(validation.digest(ROOT / path), expected)
        target = json.loads((ROOT / 'analysis/local-http-target-build.json').read_text())
        self.assertFalse(target['complete_application_linked'])
        self.assertFalse(target['hardware_tested'])
        self.assertEqual({entry['source'] for entry in target['sources']},
                         {'src/local_http.cpp', 'src/http_assets.cpp'})
        for entry in target['sources']:
            self.assertEqual(validation.digest(ROOT / entry['source']), entry['source_sha256'])
        for path, expected in target['owned_header_sha256'].items():
            self.assertEqual(validation.digest(ROOT / path), expected)

    def test_missing_or_failed_services_and_reset_binding(self):
        # These guards belong to the callable reconstruction API. They are not
        # presented as observed OEM behavior when its SDK services are absent.
        source = Path(self.build.name) / 'http_guards.cpp'
        binary = Path(self.build.name) / 'http_guards'
        source.write_text(r'''
#include "zseries/local_http.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
struct Calls {
    unsigned headers = 0, writes = 0, states = 0, conversions = 0;
    bool succeed = true;
};
static std::int32_t decimal(void* context, const char* input) {
    auto& calls = *static_cast<Calls*>(context); ++calls.conversions;
    assert(std::strcmp(input, "+42tail") == 0); return 42;
}
static bool headers(void* context, std::uint32_t status, std::int32_t size,
                    const char* type) {
    auto& calls = *static_cast<Calls*>(context); ++calls.headers;
    assert((status == 200 && size == 5) || (status == 404 && size == 0));
    assert(type == nullptr); return calls.succeed;
}
static bool write(void* context, const std::uint8_t* bytes, std::size_t size) {
    auto& calls = *static_cast<Calls*>(context); ++calls.writes;
    assert(size == 5 && std::memcmp(bytes, "PONG!", 5) == 0); return calls.succeed;
}
static bool state(void* context) {
    auto& calls = *static_cast<Calls*>(context); ++calls.states; return calls.succeed;
}
static bool failed_write(void* context, const std::uint8_t*, std::size_t) {
    ++static_cast<Calls*>(context)->writes; return false;
}
int main() {
    Calls calls;
    LocalHttpParser parser;
    parser.consume_line("POST /recipe HTTP/1.1");
    parser.request.content_length = 99;
    assert(parser.consume_line("Content-Length: +42tail") == LocalHttpResult::MissingService);
    assert(parser.request.content_length == 99);
    parser.services.context = &calls; parser.services.decimal_integer = decimal;
    parser.reset();
    assert(parser.services.context == &calls && parser.services.decimal_integer == decimal);
    assert(!parser.request_line_seen && !parser.headers_done && !parser.request.content_is_json);
    assert(parser.request.method == HttpMethod::Unknown && parser.request.content_length == 0);
    assert(parser.request.path.length == 0 && !parser.request.path.overflow);
    for (auto byte : parser.request.path.bytes) assert(byte == 0);
    assert(parser.consume_line("POST /recipe HTTP/1.1") == LocalHttpResult::Ok);
    assert(parser.consume_line("Content-Length: +42tail") == LocalHttpResult::Ok);
    assert(calls.conversions == 1 && parser.request.content_length == 42);

    LocalHttpResponseServices services; services.context = &calls;
    parser.parse_request_line("GET /ping HTTP/1.0");
    assert(route_local_http(parser.request, services) == LocalHttpResult::MissingService);
    services.headers = headers;
    assert(route_local_http(parser.request, services) == LocalHttpResult::MissingService);
    assert(calls.headers == 0 && calls.writes == 0);
    services.write = write; calls.succeed = false;
    assert(route_local_http(parser.request, services) == LocalHttpResult::ServiceFailure);
    assert(calls.headers == 1 && calls.writes == 0);
    calls.succeed = true; services.write = failed_write;
    assert(route_local_http(parser.request, services) == LocalHttpResult::ServiceFailure);
    assert(calls.headers == 2 && calls.writes == 1);
    services.write = write;
    assert(route_local_http(parser.request, services) == LocalHttpResult::Ok);
    assert(calls.headers == 3 && calls.writes == 2);

    parser.parse_request_line("GET /state HTTP/1.0");
    assert(route_local_http(parser.request, services) == LocalHttpResult::MissingService);
    services.state = state; calls.succeed = false;
    assert(route_local_http(parser.request, services) == LocalHttpResult::ServiceFailure);
    calls.succeed = true;
    assert(route_local_http(parser.request, services) == LocalHttpResult::Ok);
    assert(calls.states == 2 && calls.headers == 3 && calls.writes == 2);

    parser.parse_request_line("GET /missing HTTP/1.0");
    services.write = nullptr; services.state = nullptr;
    assert(route_local_http(parser.request, services) == LocalHttpResult::Ok);
    calls.succeed = false;
    assert(route_local_http(parser.request, services) == LocalHttpResult::ServiceFailure);
    services.headers = nullptr;
    assert(route_local_http(parser.request, services) == LocalHttpResult::MissingService);
    assert(calls.headers == 5 && calls.writes == 2 && calls.states == 2);
    assert(local_http_asset(LocalHttpAssetCount) == nullptr);
}
''')
        subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-I', str(ROOT / 'include'), str(source),
                        str(ROOT / 'src/local_http.cpp'), str(ROOT / 'src/http_assets.cpp'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
