import gzip,hashlib,json,tempfile,unittest
from hardware_console_cases import cases
from hardware_console_native import ROOT,load_native,run

class HardwareConsoleTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='hardware-console-');cls.addClassCleanup(cls.directory.cleanup);cls.native=load_native(cls.directory.name)
 def test_complete_original_diagnostics(self):
  original=json.loads(gzip.decompress((ROOT/'tests/fixtures/hardware-console-original.json.gz').read_bytes()));inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/hardware-console-inputs.json.gz').read_bytes()))
  self.assertEqual(inputs,cases());self.assertEqual(len(inputs),4)
  for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
   with self.subTest(case=index):self.assertEqual(run(self.native,case),expected)
  self.assertEqual(original['original_functions']['0x400d6ac0'],32656)
  self.assertEqual(original['original_functions']['0x400db014'],7)
 def test_scopes_continue_after_assertion_and_clock_wrap(self):
  value=run(self.native,cases()[1]);self.assertEqual(value['state'],[4,872]);self.assertEqual(sum(e.startswith('assertion|')for e in value['events']),5)
  self.assertEqual(value['events'][-2:],["scope|0|EPRM|Eeprom.h|242|EepromData::isAzureEnabled()","assertion|Assert.h|72|stackHighWaterMark > 700"])
 def test_required_services_are_not_successful_empty_callbacks(self):
  self.assertEqual(self.native.hardware_console_missing_services(),3)
 def test_evidence_matches_sources(self):
  report=json.loads((ROOT/'analysis/hardware-console-validation.json').read_text());self.assertTrue(report['all_results_match'])
  for name,value in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),value)
if __name__=='__main__':unittest.main()
