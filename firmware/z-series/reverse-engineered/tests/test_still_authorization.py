import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_still_authorization as validation


class StillAuthorizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='z-still-auth-test-');cls.addClassCleanup(cls.directory.cleanup)
        cls.binary,_=validation.build(cls.directory.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/still-authorization-original.json.gz').read_bytes()))

    def test_original_request_response_and_returning_assertions(self):
        cases=validation.input_cases();self.assertEqual([r['input'] for r in self.fixture['cases']],cases)
        for row,native in zip(self.fixture['cases'],validation.run(self.binary,cases)):
            with self.subTest(case=row['input']['id']):self.assertEqual(row['original'],native)
        self.assertTrue(any(r['original']['counter']>1 and r['original']['clock']>1000 for r in self.fixture['cases']))

    def test_original_and_target_provenance(self):
        report=json.loads((ROOT/'analysis/still-authorization-validation.json').read_text());self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested']);self.assertFalse(report['complete_application_linked'])
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/still-authorization-original.json.gz'))
        self.assertEqual(report['cases'],len(self.fixture['cases']))
        for address in ['0x4011a228','0x40119d84','0x401163a8','0x401164ac','0x400effcc','0x401118bc','0x401bf1bc','0x400db014']:
            self.assertGreater(report['original_functions_executed'][address],0)
        for path,sha in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),sha,path)
        target=json.loads((ROOT/'analysis/still-authorization-target-build.json').read_text())
        for row in target['sources']:self.assertEqual(validation.digest(ROOT/row['source']),row['source_sha256'])

    def test_required_service_failures_do_not_authorize(self):
        cases=[dict(id='no-auth',op='request',missing_authorization=True,initial_uid='',initial_date=''),dict(id='no-tls',op='request',missing_select=True,initial_uid='',initial_date=''),dict(id='tls-failed',op='request',select_fail=True),dict(id='configure-missing',op='request',configure_missing=True),dict(id='no-assert',op='body',frozen=True,missing_assertion=True,body='x'),dict(id='no-log',op='tick',pending=True,json='{',missing_log=True)]
        rows=validation.run(self.binary,cases);self.assertEqual([r['result'] for r in rows],[1,1,2,1,1,1])
        for row in rows[:4]:self.assertEqual(row['observed'],[False,False,False])
        self.assertEqual(rows[-1]['observed'],[True,False,False])
        self.assertFalse(any(e[0]=='POST' for row in rows[:4] for e in row['effects']))


if __name__=='__main__':unittest.main()
