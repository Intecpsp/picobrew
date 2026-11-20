"""Complete Display console effects and frame sequences from original instructions."""
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('display_console_validation',ROOT/'tools/validate_display_console.py')
validation=importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)

class DisplayConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='display-console-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.native=validation.load_native(cls.temp.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/display-console-original.json.gz').read_bytes()))

    def test_complete_original_callbacks_and_demonstrations(self):
        cases=validation.cases()
        self.assertEqual(len(cases),len(self.fixture['cases']))
        for case,saved in zip(cases,self.fixture['cases']):
            with self.subTest(case=case['id']):
                self.assertEqual(case,saved['input'])
                self.assertEqual(validation.native_case(self.native,case),saved['original'])
        full=next(c for c in self.fixture['cases'] if c['input']['words'][0]==0x400eaf50)
        graphics=next(c for c in self.fixture['cases'] if c['input']['words'][0]==0x400f1318)
        self.assertEqual(len(full['original']['frames']),659)
        self.assertEqual(len(graphics['original']['frames']),38)

    def test_original_image_instructions_and_source_provenance(self):
        proof=json.loads((ROOT/'analysis/display-console-validation.json').read_text())
        validation.verify_original(proof,validation.cases())
        compressed=(ROOT/'tests/fixtures/display-console-original.json.gz').read_bytes()
        self.assertEqual(hashlib.sha256(compressed).hexdigest(),proof['golden_fixture_sha256'])
        self.assertEqual(hashlib.sha256(gzip.decompress(compressed)).hexdigest(),proof['fixture_sha256'])
        self.assertTrue(proof['all_results_match'])
        for group in ['source_sha256','generator_sha256']:
            for path,expected in proof[group].items():
                self.assertEqual(validation.digest(ROOT/path),expected,path)

if __name__=='__main__':unittest.main()
