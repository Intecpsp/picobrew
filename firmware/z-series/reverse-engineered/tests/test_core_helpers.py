import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_core_helpers as validation


class CoreHelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-core-helpers-test-')
        cls.addClassCleanup(cls.build.cleanup)
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/core-helpers-original.json.gz').read_bytes()))

    def test_original_registration_download_and_session_lifecycle(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        self.assertEqual([r['input'] for r in self.fixture['cases']], validation.input_cases())
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):
                self.assertEqual(validation.native_case(self.native, row['input']), row['original'])

    def test_source_and_original_execution_provenance(self):
        report = json.loads((ROOT / 'analysis/core-helpers-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertFalse(report['complete_application_linked'])
        self.assertEqual(report['cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/core-helpers-original.json.gz'))
        self.assertGreater(report['original_functions_executed']['0x400db014'], 0)
        self.assertGreater(report['modeled_boundaries']['assertion_delay_200ms'], 0)
        for path, expected in report['source_sha256'].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)

    def test_missing_or_failed_required_services_are_explicit(self):
        case = dict(id='services', words=[3, 100, 1000, 17, 0, 0, 111, 999, 1, 1, 1, 0, 0, 42, 42, 9, 1, 1])
        for missing in [1, 2, 3]:
            result = validation.native_case(self.native, case, missing=missing)
            self.assertEqual(result['words'][4], 1)
            self.assertEqual(result['words'][5:], case['words'][5:])
            self.assertEqual(result['effects'], [])
        result = validation.native_case(self.native, case, failure=6)
        self.assertEqual(result['words'][4], 2)
        self.assertEqual(result['effects'], [6, 0])
        self.assertEqual(result['words'][7], 999)  # No fabricated completion deadline.
        case['words'][5] = 1
        result = validation.native_case(self.native, case, failure=11)
        self.assertEqual(result['words'][4], 2)
        self.assertEqual(result['words'][2:4], [1000, 17])
        self.assertEqual(result['effects'], [11, 0x14a])


if __name__ == '__main__':
    unittest.main()
