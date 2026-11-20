import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import data_cases
class DataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picoferm-data-');cls.addClassCleanup(cls.temp.cleanup);output=Path(cls.temp.name)/'data.so'
        command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/data_bridge.cpp')]
        subprocess.run(command+[str(ROOT/'src'/f'{name}.cpp') for name in ['core','hardware','data']]+['-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_uint8);cls.native.ferm_data_run.argtypes=[c.c_uint,p,p,p,c.c_uint,c.c_char_p,c.c_char_p,p,c.c_uint];cls.native.ferm_data_run.restype=c.c_uint
        cls.native.ferm_event_count.restype=c.c_uint;cls.native.ferm_event.argtypes=[c.c_uint];cls.native.ferm_event.restype=c.c_char_p
        cls.raw=(ROOT/'tests/fixtures/data-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/data-original.json.gz').read_bytes()))
    def test_original_identity_and_fixed_format_execution(self):
        self.assertEqual(self.inputs,data_cases.cases());self.assertEqual(self.fixture['cases_input_gzip_sha256'],hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(self.fixture['input_sha256'],hashlib.sha256((ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes()).hexdigest())
        for address in ['0x40202d74','0x402043c4','0x40204480','0x40201050']:self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_complete_identity_and_credentials_records_match_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                data=(c.c_uint8*112).from_buffer_copy(bytes.fromhex(row['data']));record=(c.c_uint8*236).from_buffer_copy(bytes.fromhex(row['record']));output=(c.c_uint8*128)(*([0xa5]*128));raw=bytes.fromhex(row['raw'])
                changed=self.native.ferm_data_run(row['op'],data,record,output,row['capacity'],row['first'].encode(),row['second'].encode(),(c.c_uint8*len(raw)).from_buffer_copy(raw),len(raw))
                actual=dict(id=row['id'],data=bytes(data).hex(),record=bytes(record).hex(),output=bytes(output).hex(),changed=changed,events=[self.native.ferm_event(i).decode() for i in range(self.native.ferm_event_count())])
                self.assertEqual(actual,expected)
if __name__=='__main__':unittest.main()
