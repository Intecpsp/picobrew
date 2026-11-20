import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_network_probe as validation


class NetworkProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='network-probe-tests-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native, _ = validation.load_native(cls.temporary.name)

    def test_original_wifi_statuses_and_ethernet_probe_order(self):
        fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/network-probe-original.json.gz').read_bytes()))
        self.assertEqual([row['input'] for row in fixture['cases']], validation.input_cases())
        for row in fixture['cases']:
            self.assertEqual(validation.native_case(self.native, row['input']), row['original'], row['input'])

    def test_required_service_and_failure_paths(self):
        wifi = dict(id='wifi', words=[2, 3, 1, 1, 1, 0, 0])
        ethernet = dict(id='ethernet', words=[1, 0, 1, 1, 1, 0, 0])
        self.assertEqual(validation.native_case(self.native, wifi, missing=1)['words'][6], 1)
        for missing in [2, 3, 4]:
            self.assertEqual(validation.native_case(self.native, ethernet, missing=missing)['words'][6], 1)
        for fail in [2, 3, 4]:
            result = validation.native_case(self.native, ethernet, fail=fail)
            self.assertEqual(result['words'][5:7], [0, 2])
        # A failed connection still releases its client.
        self.assertEqual(validation.native_case(self.native, ethernet, fail=3)['effects'], [2, 0, 3, 80, 4, 0])

    def test_source_and_execution_provenance(self):
        report = json.loads((ROOT / 'analysis/network-probe-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        for name, expected in report['source_sha256'].items():
            self.assertEqual(validation.digest(ROOT / name), expected, name)
        for address in ['0x400ed7d8', '0x400ed728']:
            self.assertGreater(report['original_functions_executed'][address], 0)
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/network-probe-original.json.gz'))


if __name__ == '__main__':
    unittest.main()
