from pathlib import Path
import gzip
import hashlib
import json
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_adc_continuation as validation
class AdcContinuationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='adc-continuation-test-');cls.addClassCleanup(cls.build.cleanup)
        cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/adc-continuation-original.json.gz').read_bytes()))
    def test_original_assertion_counter_delay_and_full_core_continuation(self):
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):validation.compare_case(self.native,item['input'],item['original'])
    def test_zero_timestamp_assertions_follow_smoothing_and_continue_all_stages(self):
        item=validation.case('zero-timestamp',now=0,tick=0)
        result,out=validation.native_case(self.native,item)
        self.assertEqual(result,0);self.assertEqual(out['words'][3],800);self.assertEqual(out['words'][5],11)
        self.assertEqual(out['words'][16:20],[0]*4)
        self.assertEqual([out['words'][22+i*4] for i in range(4)],[0,200,400,600])
        self.assertEqual([event[1] for event in out['events'] if event[0]==100],list(range(23)))
    def test_provenance(self):
        report=json.loads((ROOT/'analysis/adc-continuation-validation.json').read_text());validation.verify_original(report,validation.input_cases())
        compressed=(ROOT/'tests/fixtures/adc-continuation-original.json.gz').read_bytes()
        self.assertEqual(hashlib.sha256(compressed).hexdigest(),report['portable_golden_sha256'])
        self.assertEqual(hashlib.sha256(gzip.decompress(compressed)).hexdigest(),report['fixture_sha256'])
        for path,digest in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),digest,path)
        self.assertEqual(report['cases'],len(self.fixture['cases']));self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
if __name__=='__main__':unittest.main()
