"""Original client ownership/socket replay and rebuild failure boundaries."""
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_network_client as oracle


class NetworkClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='network-client-test-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native, _ = oracle.load_native(cls.temporary.name)

    def test_original_factory_tcp_tls_io_and_returning_assertions(self):
        fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/network-client-original.json.gz').read_bytes()))
        self.assertEqual(fixture['input_sha256'], oracle.IMAGE_SHA)
        cases = oracle.input_cases()
        self.assertEqual(len(cases), len(fixture['cases']))
        for item, stored in zip(cases, fixture['cases']):
            self.assertEqual(item, stored['input'])
            self.assertEqual(oracle.native_case(self.native, item), stored['original'], item)

    def test_missing_failed_allocation_and_invalid_span_do_not_succeed(self):
        for mode, expected in enumerate([1, 258, 2, 1, 3, 2]):
            self.assertEqual(self.native.network_client_guard(mode), expected, mode)

    def test_evidence_is_bound_to_this_source_and_original_image(self):
        report = json.loads((ROOT / 'analysis/network-client-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'], len(oracle.input_cases()))
        for address in ['0x400ed4d4', '0x400ed41c', '0x400db014', '0x40124aa4', '0x40124b80', '0x401249e0', '0x40124bd0']:
            self.assertIn(address, report['original_functions_executed'])
        self.assertEqual(oracle.digest(ROOT / 'tests/fixtures/network-client-original.json.gz'), report['golden_fixture_sha256'])
        for path, expected in report['source_sha256'].items():
            self.assertEqual(oracle.digest(ROOT / path), expected, path)


if __name__ == '__main__': unittest.main()
