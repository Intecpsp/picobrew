import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from backend_console_cases import cases
from backend_console_native import ROOT, load_native, run
sys.path.insert(0, str(ROOT / 'tools'))
from validate_backend_console import guarded_original


class BackendConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='backend-console-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.native = load_native(cls.directory.name)

    def test_complete_original_callbacks(self):
        original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/backend-console-original.json.gz').read_bytes()))
        inputs = json.loads(gzip.decompress((ROOT / 'tests/fixtures/backend-console-inputs.json.gz').read_bytes()))
        self.assertEqual(inputs, cases())
        self.assertEqual(len(original['cases']), 47)
        exact = 0
        for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
            with self.subTest(case=index):
                actual = run(self.native, case)
                exact += actual == expected
                self.assertEqual(actual, guarded_original(case, expected))
        self.assertEqual(exact, 43)
        for address in ['0x401188f0', '0x4011dc20', '0x400ee670', '0x4010a26c',
                        '0x4011da68', '0x4011d974', '0x400db014']:
            self.assertGreater(original['original_functions'][address], 0)

    def test_returning_assertions_wrap_and_continue(self):
        output = run(self.native, cases()[2])
        self.assertEqual(sum(event.startswith('assertion|') for event in output['events']), 11)
        self.assertEqual(output['state'], [5, 2024])
        self.assertTrue(output['events'][-1].startswith('assertion|Assert.h|72|'))
        marker = run(self.native, cases()[9])
        self.assertEqual(marker['state'][0], 1)
        self.assertTrue(any('Basic Tests ERR' in event for event in marker['events']))

    def test_missing_and_failed_services_propagate(self):
        self.assertEqual(self.native.backend_console_service_failures(), 255)

    def test_evidence_matches_current_sources(self):
        report = json.loads((ROOT / 'analysis/backend-console-validation.json').read_text())
        self.assertEqual((report['exact_cases'], report['guarded_cases']), (43, 4))
        self.assertTrue(report['all_results_match_outside_documented_guards'])
        for name, expected in report['source_sha256'].items():
            with self.subTest(path=name):
                self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected)
        target = json.loads((ROOT / 'analysis/backend-console-target-build.json').read_text())
        for row in target['sources']:
            self.assertEqual(hashlib.sha256((ROOT / row['source']).read_bytes()).hexdigest(), row['source_sha256'])
        for name, expected in target['owned_header_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected)


if __name__ == '__main__':
    unittest.main()
