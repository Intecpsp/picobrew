import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_system_tick as validation


class SystemTickTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-system-tick-test-')
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/system-tick-original.json.gz').read_bytes()))

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_timing_bits_watchdog_and_diagnostic_sequence(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        self.assertEqual([row['input'] for row in self.fixture['cases']], validation.input_cases())
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):
                self.assertEqual(validation.native_case(self.native, row['input']), row['original'])

    def test_source_evidence_and_historical_sdk_identity(self):
        report = json.loads((ROOT / 'analysis/system-tick-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/system-tick-original.json.gz'))
        for path, expected in report['source_sha256'].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        target = json.loads((ROOT / 'analysis/system-tick-target-build.json').read_text())
        self.assertFalse(target['complete_application_linked'])
        self.assertFalse(target['hardware_tested'])
        for row in target['sources']:
            self.assertEqual(validation.digest(ROOT / row['source']), row['source_sha256'])
        for field in ['owned_header_sha256', 'platform_header_sha256']:
            for path, expected in target[field].items():
                self.assertEqual(validation.digest(ROOT / path), expected, path)
        self.assertEqual(target['build_script_sha256'], validation.digest(ROOT / 'tools/build_esp32_objects.py'))
        self.assertEqual(target['esp32_dependency_pin_sha256'], validation.digest(ROOT / 'dependencies/esp32.json'))

    def test_missing_services_do_not_mutate_timing_and_log_failure_is_reported(self):
        case = dict(id='integration-failures', words=[10, 20, 30, 40],
                    average_bits=validation.bits(50.0), ticks=[[4010, 0, 5000]], assertions=9)
        for missing in [1, 2, 3, 4]:
            with self.subTest(missing=missing):
                state = validation.native_case(self.native, case, missing=missing)['snapshots'][0]
                self.assertEqual(state['result'], 1)
                self.assertEqual(state['words'], case['words'])
                self.assertEqual(state['average_bits'], case['average_bits'])
                self.assertEqual(state['events'], [])
                self.assertEqual(state['watchdog_error'], 0x1234)
        failure = validation.native_case(self.native, case, log_success=False)['snapshots'][0]
        self.assertEqual(failure['result'], 3)
        self.assertEqual(failure['words'], [4010, 4000, 30, 4000])
        self.assertEqual(failure['events'], [1, 2, 5000, 3, 5000, 4000])
        case['ticks'] = [[5, 0x102, 0xfffffff0]]
        failure = validation.native_case(self.native, case, assertion_success=False)['snapshots'][0]
        self.assertEqual(failure['result'], 2)
        self.assertEqual(failure['words'], case['words'])
        self.assertEqual(failure['assertions'], 9)
        self.assertEqual(failure['events'], [1, 6])


if __name__ == '__main__':
    unittest.main()
