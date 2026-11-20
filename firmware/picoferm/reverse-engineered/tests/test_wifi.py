import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import wifi_cases
class WifiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picoferm-wifi-');cls.addClassCleanup(cls.temp.cleanup);output=Path(cls.temp.name)/'wifi.so'
        command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/wifi_bridge.cpp')]
        subprocess.run(command+[str(ROOT/'src'/f'{name}.cpp') for name in ['core','hardware','wifi']]+['-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));cls.native.ferm_wifi_run.argtypes=[c.c_uint,c.c_uint,c.POINTER(c.c_uint32),c.c_char_p,c.c_uint]
        cls.native.ferm_event_count.restype=c.c_uint;cls.native.ferm_event.argtypes=[c.c_uint];cls.native.ferm_event.restype=c.c_char_p
        cls.native.ferm_wifi_error.restype=c.c_char_p;cls.native.ferm_wifi_label.restype=c.c_char_p
        cls.raw=(ROOT/'tests/fixtures/wifi-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/wifi-original.json.gz').read_bytes()))
    def test_original_identity_and_actual_assertions(self):
        self.assertEqual(self.inputs,wifi_cases.cases());self.assertEqual(self.fixture['cases_input_gzip_sha256'],hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(self.fixture['input_sha256'],hashlib.sha256((ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes()).hexdigest())
        for address in ['0x40202d74','0x40205b5c','0x40205e08','0x40204ad0']:self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_states_errors_and_ordered_sdk_effects_match_original(self):
        operations=dict(state=0,poll=1,fail=2,request=3,label=4,status=5,initialize=6,connect=7)
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                words=(c.c_uint32*18)(*row['words']);self.native.ferm_wifi_run(operations[row['op']],row['arg'],words,row['error'].encode(),row['empty'])
                actual=dict(id=row['id'],words=list(words),error=self.native.ferm_wifi_error().decode(),label=self.native.ferm_wifi_label().decode(),events=[self.native.ferm_event(i).decode() for i in range(self.native.ferm_event_count())])
                self.assertEqual(actual,expected)
if __name__=='__main__':unittest.main()
