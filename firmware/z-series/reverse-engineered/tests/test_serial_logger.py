import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_serial_logger as validation


class SerialLoggerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='z-serial-test-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary,_=validation.build(cls.directory.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/serial-logger-original.json.gz').read_bytes()))

    def test_original_lifecycle_and_csv_bytes(self):
        cases=validation.input_cases()
        self.assertEqual([r['input'] for r in self.fixture['cases']],cases)
        for row,actual in zip(self.fixture['cases'],validation.run(self.binary,cases)):
            with self.subTest(case=row['input']['id']):self.assertEqual(row['original'],actual)

    def test_source_and_returning_assertion_provenance(self):
        report=json.loads((ROOT/'analysis/serial-logger-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertFalse(report['complete_application_linked'])
        self.assertEqual(report['cases'],len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/serial-logger-original.json.gz'))
        for address in ['0x400db014','0x400f5d08','0x400f6064','0x400f5e40','0x400f5e74','0x400f5f40','0x400f5f94','0x400f5fcc']:
            self.assertGreater(report['original_functions_executed'][address],0)
        self.assertGreater(report['modeled_boundaries']['assertion_delay_200ms'],0)
        for path,sha in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),sha,path)

    def test_target_sources_match_the_compiled_objects(self):
        report=json.loads((ROOT/'analysis/serial-logger-target-build.json').read_text())
        self.assertFalse(report['complete_application_linked'])
        self.assertEqual({r['source'] for r in report['sources']},{'src/serial_logger.cpp','src/core_csv.cpp'})
        for row in report['sources']:self.assertEqual(validation.digest(ROOT/row['source']),row['source_sha256'])
        for path,sha in report['owned_header_sha256'].items():self.assertEqual(validation.digest(ROOT/path),sha,path)


if __name__=='__main__':unittest.main()
