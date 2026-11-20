from pathlib import Path
import gzip,hashlib,json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_adc_console as validation
class AdcConsoleTests(unittest.TestCase):
    def test_full_original_information_read_and_detect_continuation(self):
        original=json.loads(gzip.decompress((ROOT/'tests/fixtures/adc-console-original.json.gz').read_bytes()))
        with tempfile.TemporaryDirectory(prefix='adc-console-')as directory:validation.compare(original,directory)
    def test_source_and_original_fixture_provenance(self):
        report=json.loads((ROOT/'analysis/adc-console-validation.json').read_text())
        for path,expected in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),expected,path)
        self.assertEqual(validation.digest(ROOT/'tests/fixtures/adc-console-original.json.gz'),report['fixture_sha256'])
        self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
if __name__=='__main__':unittest.main()
