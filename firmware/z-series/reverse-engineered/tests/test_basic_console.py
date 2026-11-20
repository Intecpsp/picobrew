import gzip
import hashlib
import json
import sys
import tempfile
import unittest

from basic_console_cases import cases, LEAVES
from basic_console_native import ROOT, load_native, run

sys.path.insert(0, str(ROOT / 'tools'))
from validate_basic_console import expected_result
from extract_basic_console_data import outputs


class BasicConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='basic-console-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.native = load_native(cls.directory.name)

    def test_complete_original_leaves_and_aggregate(self):
        original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/basic-console-original.json.gz').read_bytes()))
        inputs = json.loads(gzip.decompress((ROOT / 'tests/fixtures/basic-console-inputs.json.gz').read_bytes()))
        self.assertEqual(inputs, cases())
        self.assertEqual(len(inputs), 152)
        self.assertEqual(len(original['cases']), len(inputs))
        for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
            with self.subTest(case=index, callback=case['callback']):
                self.assertNotIn('error', expected)
                self.assertEqual(run(self.native, case), expected_result(case, expected))
        for address in LEAVES + ['4011ed44', '4011fbac', '400db014', '400ee670', '4010a26c',
                                '401188f0', '4011dc20', '400dd2e0', '400ddee0', '400e0d34']:
            self.assertGreater(original['original_functions']['0x' + address], 0)

    def test_returning_assertions_continue_to_aggregate_error_banner(self):
        result = run(self.native, cases()[149])
        self.assertEqual(result['state'], [196, 39473])
        self.assertEqual(sum(event.startswith('assertion|') for event in result['events']), 197)
        self.assertEqual(result['events'][-6], 'invoke|0x400fe350|')
        self.assertEqual(result['events'][-5:], [
            'assertion|Core.h|473|failedCount == 0',
            'write|\x1b[31mBasic Tests ERR\x1b[0m\r\n', 'message|Basic Tests ERR',
            'log|0|CORE||0|PicoBrew::pbassertCount = 196',
            'log|1|CORE|Core.h|482|Basic Tests ran for 39 sec',
        ])

    def test_zero_clock_noise_sentinel_is_not_silently_repaired(self):
        index = LEAVES.index('4011272c') * 4 + 1
        result = run(self.native, cases()[index])
        self.assertEqual(result['state'], [1, 401])
        self.assertEqual(sum(event.startswith('assertion|') for event in result['events']), 1)
        self.assertTrue(result['events'][-1].startswith('scope|0|'))

    def test_missing_and_failed_services_are_failures(self):
        self.assertEqual(self.native.basic_console_service_failures(), 255)

    def test_immutable_original_input_data(self):
        for path, data in outputs().items():
            with self.subTest(path=path):
                self.assertEqual((ROOT / path).read_bytes(), data)
        self.assertEqual(self.native.basic_console_paragraph_probe(), 0)

    def test_evidence_matches_current_sources(self):
        report = json.loads((ROOT / 'analysis/basic-console-validation.json').read_text())
        self.assertTrue(report['all_results_match_outside_documented_guards'])
        self.assertEqual((report['exact_cases'], report['guarded_cases']), (148, 4))
        for path, expected in report['source_sha256'].items():
            with self.subTest(path=path):
                self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), expected)
        target = json.loads((ROOT / 'analysis/basic-console-target-build.json').read_text())
        for row in target['sources']:
            self.assertEqual(hashlib.sha256((ROOT / row['source']).read_bytes()).hexdigest(), row['source_sha256'])
        for path, expected in target['source_inputs'].items():
            self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), expected)


if __name__ == '__main__':
    unittest.main()
