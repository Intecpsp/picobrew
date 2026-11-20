import gzip,hashlib,json,tempfile,unittest
from msp_continuation_cases import cases,PRODUCTION_CHUNKS,UNIT_CHUNKS
from msp_continuation_native import ROOT,load_native,run

class MspContinuationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='msp-continuation-');cls.addClassCleanup(cls.directory.cleanup);cls.native=load_native(cls.directory.name);cls.cases=cases();cls.named={c['name']:c for c in cls.cases}
 def test_complete_original_bus_state_diagnostics_and_returning_assertions(self):
  original=json.loads(gzip.decompress((ROOT/'tests/fixtures/msp-continuation-original.json.gz').read_bytes()));inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/msp-continuation-inputs.json.gz').read_bytes()))
  self.assertEqual(inputs,self.cases);self.assertEqual(len(inputs),123)
  for case,expected in zip(inputs,original['cases']):
   with self.subTest(case=case['name']):self.assertEqual(run(self.native,case),expected);self.assertEqual(expected['remaining'],0)
 def test_two_guard_assertions_return_before_actual_transmission(self):
  result=run(self.native,self.named['send-0-1-13']);self.assertEqual(result['counter'],1);self.assertEqual(result['clock'],295)
  self.assertEqual([e.split('|')[0]for e in result['events'][:7]],['assertion','delay','assertion','delay','tx','delay','rx'])
 def test_handled_update_erase_failure_still_restarts_and_checks_version(self):
  case=next(c for c in self.cases if c['operation']=='setup'and "'erase_fail': 0"in c['name']and c['responses'][-1].startswith('010203'))
  result=run(self.native,case);self.assertEqual(result['result'],0)
  lines=[int(e.split('|')[2])for e in result['events']if e.startswith('assertion|')]
  self.assertEqual(lines,[0xff,0x2f0,0x2f1]);self.assertTrue(any(e.startswith('tx|72|80011c')for e in result['events']));self.assertEqual(result['remaining'],0)
 def test_update_self_test_uses_signed_shared_counter(self):
  for name,resets in (('test-update-ten',10),('test-update-prior-assertion',0),('test-update-signed-counter',10)):
   with self.subTest(case=name):self.assertEqual(run(self.native,self.named[name])['events'].count('watchdog_reset'),resets)
 def test_missing_required_services_do_not_report_success(self):
  for case,service in (('send-0-1-13','assertion'),('program-17-1','trace'),('send-2-0-13','log')):
   with self.subTest(service=service):self.assertEqual(run(self.native,self.named[case],missing=service)['result'],-10)
  self.assertEqual(run(self.native,self.named['test-update-ten'],missing='watchdog_reset')['result'],-11)
 def test_response_counts_cover_actual_embedded_images(self):
  for method,expected in ((self.native.msp_production_image,PRODUCTION_CHUNKS),(self.native.msp_unit_image,UNIT_CHUNKS)):
   image=method().contents;self.assertEqual(sum((image.segments[i].size+15)//16 for i in range(image.segment_count)),expected)
 def test_evidence_matches_sources(self):
  report=json.loads((ROOT/'analysis/msp-continuation-validation.json').read_text());self.assertTrue(report['all_results_match'])
  for name,value in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),value)
if __name__=='__main__':unittest.main()
