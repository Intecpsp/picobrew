"""Real original ping policy and explicit lwIP/DNS boundaries."""
import gzip,json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_pinger as validation
class PingerTests(unittest.TestCase):
    def test_original_state_and_ordered_sdk_calls(self):
        fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/pinger-original.json.gz').read_bytes()))
        with tempfile.TemporaryDirectory() as folder:
            native=validation.load_native(folder)
            for item in fixture['cases']:
                with self.subTest(case=item['input']['id']):self.assertEqual(validation.native_case(native,item['input']),item['original'])
    def test_execution_and_source_identity(self):
        report=json.loads((ROOT/'analysis/pinger-validation.json').read_text())
        self.assertEqual(report['input_sha256'],validation.digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin'))
        self.assertEqual(report['cases'],len(validation.input_cases()))
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/pinger-original.json.gz'))
        for path,expected in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),expected,path)
if __name__=='__main__':unittest.main()
