import gzip
import hashlib
import json
import sys
import tempfile
import unittest
from http_backend_network_cases import cases
from http_backend_network_native import ROOT, load_native, run
sys.path.insert(0, str(ROOT / 'tools'))
from validate_http_backend_network import contracts


class HttpBackendNetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='http-backend-network-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.native = load_native(cls.directory.name)

    def test_complete_original_request_receiver_parser_and_diagnostic_bodies(self):
        original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/http-backend-network-original.json.gz').read_bytes()))
        inputs = json.loads(gzip.decompress((ROOT / 'tests/fixtures/http-backend-network-inputs.json.gz').read_bytes()))
        self.assertEqual(inputs, cases())
        self.assertEqual(len(inputs), 12)
        for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
            with self.subTest(case=index):
                self.assertEqual(run(self.native, case), expected)
        for address in ['0x4011b6cc', '0x4011b250', '0x400fafd4', '0x40118060',
                        '0x40117dac', '0x40118218', '0x400db014', '0x400db438']:
            self.assertGreater(original['original_functions'][address], 0)
        self.assertEqual(original['original_functions']['0x400db014'], 231)

    def test_failed_services_restore_shared_identifier(self):
        self.assertEqual(contracts(self.native, cases()[0]), 14)

    def test_evidence_matches_sources_and_proven_target(self):
        report = json.loads((ROOT / 'analysis/http-backend-network-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertEqual((report['cases'], report['original_functions'], report['ordered_events']), (12, 179, 22810))
        for path, expected in report['source_sha256'].items():
            with self.subTest(path=path):
                self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), expected)
        target = json.loads((ROOT / 'analysis/http-backend-network-target-build.json').read_text())
        for source in target['sources']:
            self.assertEqual(hashlib.sha256((ROOT / source['source']).read_bytes()).hexdigest(), source['source_sha256'])
        for path, expected in target['owned_header_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), expected)
        sanitizer = json.loads((ROOT / 'analysis/http-backend-network-sanitizer.json').read_text())
        self.assertTrue(sanitizer['passed'])
        self.assertEqual(sanitizer['failure_contracts'], 14)
        for path, expected in sanitizer['source_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), expected)


if __name__ == '__main__':
    unittest.main()
