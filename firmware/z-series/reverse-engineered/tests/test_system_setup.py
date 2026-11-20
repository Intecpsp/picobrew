"""System startup continuation and shared diagnostic behavior against original code."""
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_system_setup as oracle


class SystemSetupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='system-setup-test-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native, _ = oracle.load_native(cls.temporary.name)

    def test_complete_original_setup_and_returning_assertions(self):
        fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/system-setup-original.json.gz').read_bytes()))
        self.assertEqual(fixture['input_sha256'], oracle.IMAGE_SHA)
        cases = oracle.input_cases()
        self.assertEqual(len(cases), len(fixture['cases']))
        for item, stored in zip(cases, fixture['cases']):
            self.assertEqual(item, stored['input'])
            self.assertEqual(oracle.native_case(self.native, item), stored['original'], item)

    def test_missing_services_and_failed_adapters_do_not_report_setup_success(self):
        item = dict(input=[123, 0, 1, 1, 0, 0, 0])
        for missing in [1, 2]:
            actual = oracle.native_case(self.native, item, missing=missing)
            self.assertEqual(actual['result'], 1)
            self.assertEqual(actual['events'], [])
        for missing in [3, 4, 5]:
            actual = oracle.native_case(self.native, item, missing=missing)
            self.assertEqual(actual['result'], 3)
            self.assertEqual(actual['output'][1], 0)
        for stage in range(12):
            actual = oracle.native_case(self.native, item, failure=stage)
            self.assertEqual(actual['result'], 2)
            self.assertEqual(actual['events'][-5], stage)

    def test_source_and_fixture_identities(self):
        report = json.loads((ROOT / 'analysis/system-setup-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'], len(oracle.input_cases()))
        self.assertEqual(report['golden_fixture_sha256'],
            oracle.digest(ROOT / 'tests/fixtures/system-setup-original.json.gz'))
        self.assertIn('0x400db014', report['original_functions_executed'])
        self.assertIn('0x40112f88', report['original_functions_executed'])
        self.assertIn('0x400f7f48', report['original_functions_executed'])
        for path, expected in report['source_sha256'].items():
            self.assertEqual(oracle.digest(ROOT / path), expected, path)


if __name__ == '__main__':
    unittest.main()
