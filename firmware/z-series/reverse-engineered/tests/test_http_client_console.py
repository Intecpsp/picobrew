import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_http_client_console as validation
from http_client_console_native import build,execute
from http_client_console_cases import cases

class HttpClientConsoleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory(prefix='http-console-native-')
        cls.fn=build(Path(cls.temporary.name));cls.fixture=validation.load_fixture()
    @classmethod
    def tearDownClass(cls):cls.temporary.cleanup()
    def test_complete_original_execution(self):
        self.assertEqual(len(self.fixture['original']),len(cases()))
        for expected,item in zip(self.fixture['original'],cases()):
            with self.subTest(case=item['id']):self.assertEqual(execute(self.fn,item),expected)
    def test_missing_and_rejected_callbacks(self):self.assertEqual(validation.fail_contracts(self.fn),39)
    def test_bound_evidence_and_private_resources(self):
        report=json.loads(validation.REPORT.read_text())
        for name,expected in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/name),expected,name)
        self.assertEqual(validation.digest(validation.FIXTURE),report['fixture_sha256'])
        self.assertEqual(hashlib.sha256(validation.serialized_inputs()).hexdigest(),report['inputs_sha256'])
        self.assertEqual(validation.verify_private_resources(),5)
        self.assertTrue(report['original_execution']['secret_payloads_hashed'])

if __name__=='__main__':unittest.main()
