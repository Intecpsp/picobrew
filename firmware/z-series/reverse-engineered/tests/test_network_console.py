import gzip
import hashlib
import json
import tempfile
import unittest
from network_console_cases import cases
from network_console_native import ROOT, load_native, run


class NetworkConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='network-console-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.native = load_native(cls.directory.name)

    def test_original_orchestration_and_binary_bodies(self):
        original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/network-console-original.json.gz').read_bytes()))
        inputs = json.loads(gzip.decompress((ROOT / 'tests/fixtures/network-console-inputs.json.gz').read_bytes()))
        self.assertEqual(inputs, cases())
        self.assertEqual(len(inputs), 64)
        for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
            with self.subTest(case=index):
                self.assertEqual(run(self.native, case), expected)
        for address in ['0x401024a8', '0x400e5ad0', '0x400e5b58', '0x4011bffc',
                        '0x4011c2f8', '0x4010a364', '0x4010a3d8', '0x4011fbbc',
                        '0x400ee670', '0x4010a26c', '0x400db014']:
            self.assertGreater(original['original_functions'][address], 0)

    def test_required_and_failed_callbacks_propagate(self):
        self.assertEqual(self.native.network_console_failures(), 255)

    def test_evidence_matches_current_sources(self):
        report = json.loads((ROOT / 'analysis/network-console-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertEqual((report['cases'], report['returning_assertions']), (64, 148))
        for name, expected in report['source_sha256'].items():
            with self.subTest(path=name):
                self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected)
        target = json.loads((ROOT / 'analysis/network-console-target-build.json').read_text())
        for row in target['sources']:
            self.assertEqual(hashlib.sha256((ROOT / row['source']).read_bytes()).hexdigest(), row['source_sha256'])
        for name, expected in target['owned_header_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected)


if __name__ == '__main__':
    unittest.main()
