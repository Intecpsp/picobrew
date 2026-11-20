import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_azure_console as validation
from azure_console_native import build, execute
from azure_console_cases import cases


class AzureConsoleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='azure-console-native-')
        cls.fn = build(Path(cls.temporary.name))
        cls.fixture = validation.load_fixture()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_complete_original_execution(self):
        self.assertEqual(len(self.fixture['original']), len(cases()))
        for expected, item in zip(self.fixture['original'], cases()):
            with self.subTest(case=item['id']):
                self.assertEqual(execute(self.fn, item), expected)

    def test_missing_and_rejected_callbacks(self):
        self.assertEqual(validation.fail_contracts(self.fn), 32)

    def test_zero_deadline_assertion_continues(self):
        rows = {item['id']: item for item in self.fixture['original']}
        zero, nearby = rows['zero-loaded-deadline'], rows['loaded-deadline-near-wrap']
        self.assertEqual(zero['result'], 0)
        self.assertEqual(zero['state'][9], nearby['state'][9] + 1)
        self.assertEqual(zero['state'][8], nearby['state'][8] + 600)
        self.assertEqual(sum(event[0] == 16 for event in zero['events']), 1)

    def test_bound_evidence_and_private_key(self):
        report = json.loads(validation.REPORT.read_text())
        for name, expected in report['source_sha256'].items():
            self.assertEqual(validation.digest(ROOT / name), expected, name)
        self.assertEqual(validation.digest(validation.FIXTURE), report['fixture_sha256'])
        self.assertEqual(hashlib.sha256(validation.serialized_inputs()).hexdigest(), report['inputs_sha256'])
        self.assertEqual(validation.verify_private_resources(), 5)
        self.assertTrue(report['original_execution']['secret_payloads_hashed'])


if __name__ == '__main__':
    unittest.main()
