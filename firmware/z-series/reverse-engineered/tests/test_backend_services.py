import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_services as validation


class BackendServicesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='z-backend-test-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary, _ = validation.build(cls.directory.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/backend-services-original.json.gz').read_bytes()))

    def test_complete_original_calls_and_documented_memory_differences(self):
        cases = validation.json_cases() + validation.lifecycle_cases()
        self.assertEqual([r['input'] for r in self.fixture['cases']], cases)
        actual = validation.run(self.binary, cases)
        for row, native in zip(self.fixture['cases'], actual):
            with self.subTest(case=row['input']['id']):
                self.assertEqual(validation.expected(row['input'], row['original']), native)
        diverged = [r for r in self.fixture['cases'] if 'terminal' in r['original']]
        self.assertEqual({r['input']['id'] for r in diverged}, validation.DIVERGENT)

    def test_execution_and_source_provenance(self):
        report = json.loads((ROOT / 'analysis/backend-services-validation.json').read_text())
        self.assertFalse(report['all_results_match'])
        self.assertTrue(report['all_documented_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertFalse(report['complete_application_linked'])
        self.assertEqual(report['cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/backend-services-original.json.gz'))
        for address in ['0x400db014', '0x4011c30c', '0x4011c4f4', '0x401179a8', '0x400f400c', '0x400fa400']:
            self.assertGreater(report['original_functions_executed'][address], 0)
        for path, sha in report['source_sha256'].items():
            self.assertEqual(validation.digest(ROOT / path), sha, path)

    def test_missing_services_do_not_fabricate_success(self):
        output = validation.run(self.binary, [dict(id='required-services', op='service_checks')])[0]
        for key in ['z_missing_network', 'http_missing_network', 'azure_missing_transport', 'azure_missing_hmac']:
            self.assertEqual(output[key], 1, key)
        self.assertFalse(output['z_ready'])
        self.assertEqual(output['http_state'], 0)
        self.assertEqual(output['password'], '')
        self.assertEqual(output['console_missing_callbacks'], 2)
        self.assertEqual(output['console_entries'], 0)
        self.assertEqual(output['effects'], [])


if __name__ == '__main__':
    unittest.main()
