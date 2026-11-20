import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import backend_cases
class BackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picoferm-backend-');cls.addClassCleanup(cls.temp.cleanup);output=Path(cls.temp.name)/'backend.so'
        command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/backend_bridge.cpp')]
        subprocess.run(command+[str(ROOT/'src'/f'{name}.cpp') for name in ['core','hardware','data','backend']]+['-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_uint8);w=c.POINTER(c.c_uint32);s=c.c_char_p;cls.native.ferm_backend_run.argtypes=[c.c_uint,p,p,w,w,c.c_uint,w,c.c_uint,s,s,s,s,s]
        cls.native.ferm_event_count.restype=c.c_uint;cls.native.ferm_event.argtypes=[c.c_uint];cls.native.ferm_event.restype=c.c_char_p
        for name in ['error','sent']:getattr(cls.native,'ferm_backend_'+name).restype=c.c_char_p
        for name in ['clock','position']:getattr(cls.native,'ferm_backend_'+name).restype=c.c_uint32
        cls.raw=(ROOT/'tests/fixtures/backend-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/backend-original.json.gz').read_bytes()))
    def test_original_identity_and_actual_request_continuations(self):
        self.assertEqual(self.inputs,backend_cases.cases());self.assertEqual(self.fixture['cases_input_gzip_sha256'],hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(self.fixture['input_sha256'],hashlib.sha256((ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes()).hexdigest())
        for address in ['0x40202d74','0x40202d8c','0x40207878','0x402076c4','0x40207584','0x40201050']:self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_complete_backend_records_requests_and_effects_match_original(self):
        operations=dict(headers=0,prefix=1,argument=2,send=3,fail=4,sync=5,state=6,logs=7,complete=8,poll=9,sequence=10)
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                data=(c.c_uint8*0x454).from_buffer_copy(bytes.fromhex(row['data']));record=(c.c_uint8*236).from_buffer_copy(bytes.fromhex(row['record']));items=(c.c_uint32*len(row['items']))(*row['items']);connections=(c.c_uint32*len(row['connect']))(*row['connect']);words=(c.c_uint32*7)(row['now'],row['tick'],row['rate'],row['voltage'],row['logs'],row['connected'],row['polls'])
                self.native.ferm_backend_run(operations[row['op']],data,record,words,items,len(items),connections,len(connections),*[row[name].encode() for name in ['text','key','value','response','error']])
                actual=dict(id=row['id'],data=bytes(data).hex(),error=self.native.ferm_backend_error().decode(),sent=self.native.ferm_backend_sent().decode(),clock=self.native.ferm_backend_clock(),position=self.native.ferm_backend_position(),events=[self.native.ferm_event(i).decode() for i in range(self.native.ferm_event_count())])
                self.assertEqual(actual,expected)
if __name__=='__main__':unittest.main()
