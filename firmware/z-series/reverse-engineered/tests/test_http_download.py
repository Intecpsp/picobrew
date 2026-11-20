"""HTTP receiver continuations compared with original firmware execution."""
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_http_download as oracle


class HttpDownloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='http-download-test-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native, _ = oracle.load_native(cls.temporary.name)

    def test_original_headers_body_completion_and_returning_assertions(self):
        fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/http-download-original.json.gz').read_bytes()))
        self.assertEqual(fixture['input_sha256'], oracle.IMAGE_SHA)
        cases = oracle.input_cases(); self.assertEqual(len(cases), len(fixture['cases']))
        for item, stored in zip(cases, fixture['cases']):
            self.assertEqual(item, stored['input'])
            self.assertEqual(oracle.native_case(self.native, item), stored['original'], item)

    def test_missing_services_corrupt_index_and_callback_ownership(self):
        for mode, expected in enumerate([1, 1, 259, 3, 0, 0]):
            self.assertEqual(self.native.http_download_guard(mode), expected, mode)

    def test_source_fixture_and_original_identities(self):
        report = json.loads((ROOT / 'analysis/http-download-validation.json').read_text())
        self.assertTrue(report['all_results_match']); self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'], len(oracle.input_cases()))
        for address in ['0x400da534', '0x400d093c', '0x400d8068', '0x400da5f0', '0x400e5cfc', '0x400e5c4c', '0x400db014']:
            self.assertIn(address, report['original_functions_executed'])
        self.assertEqual(oracle.digest(ROOT / 'tests/fixtures/http-download-original.json.gz'), report['golden_fixture_sha256'])
        for path, expected in report['source_sha256'].items(): self.assertEqual(oracle.digest(ROOT / path), expected, path)


if __name__ == '__main__': unittest.main()
