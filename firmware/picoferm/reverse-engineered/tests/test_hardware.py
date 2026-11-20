import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import hardware_cases
class HardwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picoferm-hardware-');cls.addClassCleanup(cls.temp.cleanup);output=Path(cls.temp.name)/'hardware.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/hardware_bridge.cpp'),str(ROOT/'src/core.cpp'),str(ROOT/'src/hardware.cpp'),'-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_uint32);cls.native.ferm_hardware_run.argtypes=[c.c_uint,p,p,c.c_uint,c.c_char_p]
        cls.native.ferm_result.argtypes=[c.c_uint];cls.native.ferm_result.restype=c.c_uint32
        cls.native.ferm_event_count.restype=c.c_uint;cls.native.ferm_event.argtypes=[c.c_uint];cls.native.ferm_event.restype=c.c_char_p
        cls.raw=(ROOT/'tests/fixtures/hardware-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/hardware-original.json.gz').read_bytes()))
    def test_original_identity_and_actual_adc_conversion_execution(self):
        self.assertEqual(self.inputs,hardware_cases.cases());self.assertEqual(self.fixture['cases_input_gzip_sha256'],hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(self.fixture['input_sha256'],hashlib.sha256((ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes()).hexdigest())
        for address in ['0x40202d74','0x40202d8c','0x40203210','0x402143d0','0x402056a4']:self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_complete_hardware_state_and_ordered_effects_match_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                words=(c.c_uint32*24)(*row['words']);reads=(c.c_uint32*len(row['reads']))(*row['reads'])
                self.native.ferm_hardware_run(row['op'],words,reads,len(reads),row['text'].encode())
                actual=dict(id=row['id'],words=list(words),events=[self.native.ferm_event(i).decode() for i in range(self.native.ferm_event_count())])
                if 'result' in expected:actual['result']=self.native.ferm_result(0)
                self.assertEqual(actual,expected)
if __name__=='__main__':unittest.main()
