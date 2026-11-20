import gzip,hashlib,json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_still_continuation as validation
class StillContinuationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='still-continuation-');cls.addClassCleanup(cls.directory.cleanup)
  cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/still-continuation-original.json.gz').read_bytes()))
  cls.libraries={name:validation.load_native(name,cls.directory.name)[0]for name in validation.MODULES}
 def test_all_original_controller_client_parser_and_statistics_continuations(self):
  for name,part in self.fixture['groups'].items():
   self.assertEqual(part['inputs'],validation.input_cases(name));self.assertGreater(part['execution']['original_functions_executed']['0x400db014'],0)
   for case,expected in zip(part['inputs'],part['original']):
    with self.subTest(group=name,case=case['id']):self.assertEqual(validation.native_case(name,self.libraries[name],case),expected)
 def test_wrapping_assertions_and_full_bad_sample_continuation(self):
  cases={r['id']:r for r in validation.input_cases('still-client')}
  result=validation.native_case('still-client',self.libraries['still-client'],cases['continuation-bad-data-wrap'])
  self.assertEqual(result['clock'],672);self.assertEqual(result['counter'],3)
  self.assertEqual(sum(t.startswith('assertion|')for t in result['trace']),4)
  self.assertEqual(result['state'][16],0xffffff80)
 def test_missing_and_failed_diagnostic_ports_propagate_through_http(self):
  binary=Path(self.directory.name)/'contracts'
  sources=['tests/still_continuation_contracts.cpp','src/pico_still.cpp','src/pico_still_sensors.cpp','src/still_client.cpp','src/still_http.cpp','src/http_client.cpp','src/enums.cpp','src/eeprom.cpp']
  subprocess.run(['c++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-fno-fast-math','-ffp-contract=off','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'include')]+[str(ROOT/p)for p in sources]+['-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
 def test_evidence_matches_current_source_and_target_objects(self):
  report=json.loads((ROOT/'analysis/still-continuation-validation.json').read_text());self.assertTrue(report['all_results_match'])
  for name,expected in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(validation.digest(ROOT/name),expected)
  self.assertEqual(validation.digest(ROOT/'tests/fixtures/still-continuation-original.json.gz'),report['fixture_sha256'])
  self.assertEqual(validation.digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin'),report['input_sha256'])
  target=json.loads((ROOT/'analysis/still-continuation-target-build.json').read_text())
  for row in target['sources']:self.assertEqual(validation.digest(ROOT/row['source']),row['source_sha256'])
if __name__=='__main__':unittest.main()
