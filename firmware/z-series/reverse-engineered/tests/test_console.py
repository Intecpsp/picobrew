"""Console behavior against complete original execution, including diagnostics."""
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_console as oracle


class ConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='console-test-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native, _ = oracle.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/console-original.json.gz').read_bytes()))

    def compare_modes(self, modes):
        cases = oracle.input_cases()
        self.assertEqual(len(cases), len(self.fixture['cases']))
        for item, stored in zip(cases, self.fixture['cases']):
            if item['data'][0] in modes:
                self.assertEqual(item, stored['input'])
                self.assertEqual(oracle.native_case(self.native, item), stored['original'], item)

    def test_registration_dispatch_and_complete_recursive_help(self):
        self.compare_modes([0, 1, 2, 3])

    def test_serial_buffer_delimiters_and_all_256_input_bytes(self):
        self.compare_modes([4])

    def test_single_slot_queue_preserves_pending_command(self):
        self.compare_modes([5])

    def test_explicit_malformed_input_guards_and_payload_ownership(self):
        for mode, expected in enumerate([2, 4, 4, 5, 3, 1, 4, 0]):
            self.assertEqual(self.native.console_guard_case(mode), expected, mode)

    def test_source_and_original_identities(self):
        report = json.loads((ROOT / 'analysis/console-validation.json').read_text())
        self.assertEqual(self.fixture['input_sha256'], oracle.IMAGE_SHA)
        self.assertEqual(report['cases'], len(oracle.input_cases()))
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        for address in ['0x400db014', '0x4011005c', '0x400d4e10', '0x400fa21c', '0x400f08e8']:
            self.assertIn(address, report['original_functions_executed'])
        self.assertEqual(oracle.digest(ROOT / 'tests/fixtures/console-original.json.gz'), report['golden_fixture_sha256'])
        for path, expected in report['source_sha256'].items():
            self.assertEqual(oracle.digest(ROOT / path), expected, path)


if __name__ == '__main__':
    unittest.main()
