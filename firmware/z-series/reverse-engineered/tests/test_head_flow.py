import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_head_flow as validation

class HeadFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-head-flow-test-')
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / 'tests/fixtures/head-flow-original.json').read_text())

    @classmethod
    def tearDownClass(cls): cls.build.cleanup()

    def test_original_priority_command_routing_and_alarm_stop(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):
                self.assertEqual(validation.native_case(self.native, item['input']), item['original'])

    def test_evidence_identity_and_scope(self):
        report = json.loads((ROOT / 'analysis/head-flow-validation.json').read_text())
        self.assertTrue(report['all_results_match']); self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['input_sha256'], validation.IMAGE_SHA)
        self.assertEqual(report['cases'], len(validation.input_cases()))
        self.assertEqual(report['cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/head-flow-original.json'))
        for path, expected in report['source_sha256'].items():
            with self.subTest(path=path): self.assertEqual(validation.digest(ROOT / path), expected)

    def test_missing_and_failed_services_are_errors(self):
        source = Path(self.build.name) / 'head_guards.cpp'; binary = Path(self.build.name) / 'head_guards'
        source.write_text(r'''
#include "zseries/head_flow.hpp"
#include <cassert>
using namespace zseries::oem;
static std::uint32_t now(void*){return 123;}
static bool pending(void*,HeadEventKind){return false;}
static bool handler(void*,HeadEventKind){return true;}
static bool failed(void*,const HeadAction&){return false;}
int main(){
    HeadFlow head;head.observed.core=CoreStateId::Running;
    assert(head.tick(100)==HeadFlowResult::MissingService);
    assert(head.synchronize()==HeadFlowResult::MissingService);
    assert(head.dispatch(CoreCommandId::Stop)==HeadFlowResult::MissingService);
    assert(head.alarm(true)==HeadFlowResult::MissingService);
    head.services.millis=now;head.services.pending=pending;
    head.services.handle_event=handler;head.services.perform=failed;
    assert(head.synchronize()==HeadFlowResult::ServiceFailure);
    assert(head.alarm(true)==HeadFlowResult::ServiceFailure);
    assert(!head.alarm_timer.active);
    assert(head.dispatch(CoreCommandId::Stop)==HeadFlowResult::ServiceFailure);
    head.alarm_duration_ms=0;
    assert(head.alarm(true)==HeadFlowResult::InvalidDuration);
    assert(head.start_transient(0)==HeadFlowResult::InvalidDuration);
    assert(!head.transient_timer.active);
}
''')
        subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(source),
                        str(ROOT / 'src/head_flow.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == '__main__': unittest.main()
