import gzip,hashlib,json,tempfile,unittest
from software_console_cases import cases
from software_console_native import ROOT,load_native,run
class SoftwareConsoleTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='software-console-');cls.addClassCleanup(cls.directory.cleanup);cls.native=load_native(cls.directory.name)
 def test_complete_original_callbacks_and_aggregates(self):
  original=json.loads(gzip.decompress((ROOT/'tests/fixtures/software-console-original.json.gz').read_bytes()));inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/software-console-inputs.json.gz').read_bytes()))
  self.assertEqual(inputs,cases());self.assertEqual(len(inputs),14)
  for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
   with self.subTest(case=index):self.assertEqual(run(self.native,case),expected)
  for address in ['0x400e0e70','0x400e076c','0x400e09f8','0x400eea38','0x400f610c','0x400f5fcc','0x4010d824','0x401009bc','0x40119eb8','0x40119d84','0x401163a8','0x4011a204','0x4011a1fc','0x400db014']:
   self.assertGreater(original['original_functions'][address],0)
 def test_aggregate_continues_after_assertions_and_wrap(self):
  result=run(self.native,cases()[11]);self.assertEqual(result['state'],[13,2672]);self.assertEqual(sum(e.startswith('assertion|')for e in result['events']),14)
  self.assertEqual(result['events'][-2:],["scope|0|BSTIL|PicoStill.h|264|BackendClient_T::parseJson() Test3","assertion|Assert.h|72|stackHighWaterMark > 700"])
 def test_missing_services_are_failures(self):self.assertEqual(self.native.software_console_missing_services(),63)
 def test_evidence_matches_current_sources(self):
  report=json.loads((ROOT/'analysis/software-console-validation.json').read_text());self.assertTrue(report['all_results_match'])
  for name,sha in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),sha)
  target=json.loads((ROOT/'analysis/software-console-target-build.json').read_text())
  for row in target['sources']:self.assertEqual(hashlib.sha256((ROOT/row['source']).read_bytes()).hexdigest(),row['source_sha256'])
if __name__=='__main__':unittest.main()
