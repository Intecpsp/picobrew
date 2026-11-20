import gzip,json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_http_client_continuation as validation

class HttpClientContinuationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='http-continuation-test-');cls.addClassCleanup(cls.directory.cleanup)
        cls.native,_=validation.load_native(cls.directory.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/http-client-continuation-original.json.gz').read_bytes()))
    def test_every_original_assertion_and_socket_continuation(self):
        self.assertEqual(self.fixture['inputs'],validation.input_cases())
        self.assertGreater(self.fixture['execution']['original_functions_executed']['0x400db014'],0)
        for item,expected in zip(self.fixture['inputs'],self.fixture['original']):
            with self.subTest(case=item['id']):self.assertEqual(validation.native_case(self.native,item),expected)
    def test_wrapping_counter_and_clock_keep_captured_timing(self):
        item=next(i for i in self.fixture['inputs']if i['id']=='timing-4294967280-0-3')
        result=validation.native_case(self.native,item)
        self.assertEqual(result['state'][19],0xfffffff0)
        self.assertEqual(result['state'][34:36],[184,0])
        self.assertEqual([r[0]for r in result['events']],[18,16,17])
    def test_missing_and_failed_callbacks_do_not_succeed(self):
        binary=Path(self.directory.name)/'contracts'
        subprocess.run(['c++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'include')]+
            [str(ROOT/p)for p in ['src/http_client.cpp','src/enums.cpp','src/diagnostics.cpp','tests/http_client_continuation_contracts.cpp']]+['-o',str(binary)],check=True)
        environment=os.environ.copy();environment.setdefault('ASAN_OPTIONS','detect_leaks=0')
        subprocess.run([str(binary)],check=True,env=environment)
    def test_evidence_binds_current_inputs_and_original_image(self):
        report=json.loads((ROOT/'analysis/http-client-continuation-validation.json').read_text())
        self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(validation.input_cases()))
        self.assertEqual(report['fixture_sha256'],validation.digest(ROOT/'tests/fixtures/http-client-continuation-original.json.gz'))
        self.assertEqual(report['input_sha256'],validation.digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin'))
        for name,expected in report['source_sha256'].items():
            with self.subTest(path=name):self.assertEqual(validation.digest(ROOT/name),expected)
if __name__=='__main__':unittest.main()
