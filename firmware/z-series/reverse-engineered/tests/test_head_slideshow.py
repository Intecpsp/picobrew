import gzip,hashlib,json,tempfile,unittest
from head_slideshow_native import ROOT,load_native,run
from head_slideshow_cases import cases
class HeadSlideshowTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=tempfile.TemporaryDirectory(prefix='head-slideshow-');cls.addClassCleanup(cls.directory.cleanup);cls.native=load_native(cls.directory.name)
 def test_complete_original_renderer_lifecycle_and_selftest_results(self):
  inputs=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-slideshow-inputs.json.gz').read_bytes()));original=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-slideshow-original.json.gz').read_bytes()))
  self.assertEqual(inputs,cases());self.assertEqual(len(inputs),328);self.assertEqual(len(inputs),len(original['cases']))
  for index,(case,expected)in enumerate(zip(inputs,original['cases'])):
   with self.subTest(case=index):self.assertEqual(run(self.native,case),expected)
 def test_invalid_selection_asserts_twice_and_continues(self):
  c=next(c for c in cases()if c['operation']==1 and c['select']==0xffffffff);r=run(self.native,c)
  self.assertEqual(r['state'],[1,0x307,9,1177]);self.assertEqual(r['slideshow'][0],0);self.assertEqual(len([e for e in r['events']if e.startswith('assertion|')]),2)
 def test_active_countdown_retains_original_unsigned_wrap(self):
  c=next(c for c in cases()if c['operation']==1 and c['left']==1 and c['tick2']==2777);r=run(self.native,c)
  self.assertEqual(r['entries'][0][1],0xffffffff);self.assertEqual(r['entries'][0][4],1)
 def test_required_services_are_not_successful_noops(self):
  self.assertEqual(self.native.head_slideshow_missing_services(),31)
 def test_evidence_tracks_sources(self):
  report=json.loads((ROOT/'analysis/head-slideshow-validation.json').read_text());self.assertTrue(report['all_results_match'])
  for name,digest in report['source_sha256'].items():
   with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),digest)
if __name__=='__main__':unittest.main()
