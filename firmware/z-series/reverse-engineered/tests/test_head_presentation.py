from pathlib import Path
import gzip,hashlib,json,tempfile,unittest
from head_presentation_native import ROOT,load_native,run
from head_presentation_cases import cases
class HeadPresentationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='head-presentation-');cls.addClassCleanup(cls.directory.cleanup);cls.native=load_native(cls.directory.name)
 def test_original_framebuffers_and_returning_assertions(self):
  original=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-presentation-original.json.gz').read_bytes()));inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-presentation-inputs.json.gz').read_bytes()))
  self.assertEqual(inputs,cases());self.assertEqual(len(inputs),71);self.assertEqual(len(original['cases']),len(inputs))
  for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
   with self.subTest(case=index):self.assertEqual(run(self.native,case),expected)
 def test_empty_hardware_view_returns_after_original_assertion(self):
  case=cases()[0];self.assertEqual(case['extra'],0);value=run(self.native,case)
  self.assertEqual(value['state'],[0,0x5e,8,977])
  pixels=bytes(((i*73+17)^(i>>3))&255 for i in range(4096));self.assertEqual(value['pixels_sha256'],hashlib.sha256(pixels).hexdigest())
 def test_evidence_matches_sources(self):
  report=json.loads((ROOT/'analysis/head-presentation-validation.json').read_text());self.assertTrue(report['all_framebuffers_match'])
  for name,value in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),value)
if __name__=='__main__':unittest.main()
