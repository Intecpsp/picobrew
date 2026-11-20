import gzip,hashlib,json,tempfile,unittest
from eeprom_continuation_cases import cases
from eeprom_continuation_native import ROOT,load_native,run
class EepromContinuationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='eeprom-continuation-');cls.addClassCleanup(cls.directory.cleanup);cls.native=load_native(cls.directory.name);cls.cases=cases()
 def test_full_original_matrix_including_all_assertion_continuations(self):
  original=json.loads(gzip.decompress((ROOT/'tests/fixtures/eeprom-continuation-original.json.gz').read_bytes()));inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/eeprom-continuation-inputs.json.gz').read_bytes()))
  self.assertEqual(inputs,self.cases);self.assertEqual(len(inputs),2363)
  for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
   with self.subTest(index=index,action=case['action']):self.assertEqual(run(self.native,case),expected)
 def test_reset_assertions_both_return_and_commit_continues(self):
  case=next(c for c in self.cases if c['action']==8 and not c['initialized']and c['actual']==512)
  result=run(self.native,case)
  self.assertEqual([int(e.split('|')[2])for e in result['events']if e.startswith('assertion|')],[0x158,0x160])
  self.assertTrue(any(e.startswith('commit|')for e in result['events']));self.assertEqual(result['initialized'],0)
 def test_undersized_requested_buffer_assertion_still_registers(self):
  case=next(c for c in self.cases if c['group']=='setup_size'and c['requested']==0)
  result=run(self.native,case);self.assertEqual(result['events'][-1],'register');self.assertEqual(result['initialized'],1)
 def test_required_diagnostics_and_full_self_test_binding(self):
  self.assertEqual(self.native.eeprom_continuation_missing_services(),15)
 def test_evidence_matches_sources(self):
  report=json.loads((ROOT/'analysis/eeprom-continuation-validation.json').read_text());self.assertTrue(report['all_results_match'])
  for name,value in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),value)
if __name__=='__main__':unittest.main()
