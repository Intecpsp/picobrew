import gzip,hashlib,json,sys,tempfile,unittest
from pathlib import Path
from runtime_continuation_native import ROOT,load_native,run
import program_runtime_cases as inputs
import run_msp_emulation as common
sys.path.insert(0,str(ROOT/'tools'))
from validate_runtime_continuation import exact
class RuntimeContinuationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='runtime-continuation-');cls.addClassCleanup(cls.directory.cleanup);cls.native=load_native(cls.directory.name)
  cls.original=json.loads(gzip.decompress((ROOT/'tests/fixtures/runtime-continuation-original.json.gz').read_bytes()));cls.cases=inputs.cases()
 def test_every_original_case_finishes_including_all_prior_assertions(self):
  self.assertEqual(len(self.cases),5792);self.assertEqual(len(self.original['cases']),len(self.cases))
  for index,(case,expected)in enumerate(zip(self.cases,self.original['cases'])):
   with self.subTest(index=index,group=case['group']):self.assertTrue(exact(expected,run(self.native,case)))
  self.assertEqual(sum(bool(c['diagnostics'])for c in self.original['cases']),1023)
  self.assertEqual(sum(len(c['diagnostics'])for c in self.original['cases']),1142)
 def test_nested_still_assertions_preserve_timestamp_between_delays(self):
  result=run(self.native,self.cases[1575]);self.assertEqual(result['diagnostics'],['Runtime.h|544|rtState.isRefDefined()','RuntimeState.h|1050|isRefDefined()']);self.assertEqual(result['counter'],1);self.assertEqual(result['clock'],2400)
  self.assertEqual(result['words']['step_date'],2200)
 def test_missing_and_failed_services_do_not_succeed(self):self.assertEqual(self.native.runtime_continuation_service_contracts(),1023)
 def test_original_function_bytes_and_current_sources(self):
  report=json.loads((ROOT/'analysis/runtime-continuation-validation.json').read_text());self.assertTrue(report['all_results_match'])
  for name,sha in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),sha)
  functions={f['entry']for f in self.original['original_functions']};self.assertIn(0x400db014,functions);self.assertIn(0x4012d42c,functions)
  raw=common.INPUT.read_bytes();self.assertEqual(hashlib.sha256(raw).hexdigest(),report['input_sha256'])
  segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in common.images.parse_image(raw)['segments']]
  for function in self.original['original_functions']:
   for span in function['ranges']:self.assertEqual(hashlib.sha256(common.read_mapped(raw,segments,span['address'],span['size'])).hexdigest(),span['sha256'])
  target=json.loads((ROOT/'analysis/runtime-continuation-target-build.json').read_text())
  for row in target['sources']:self.assertEqual(hashlib.sha256((ROOT/row['source']).read_bytes()).hexdigest(),row['source_sha256'])
if __name__=='__main__':unittest.main()
