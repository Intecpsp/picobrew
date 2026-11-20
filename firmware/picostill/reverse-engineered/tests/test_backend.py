"""Full original backend state/request/parser continuation comparison."""
import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import backend_cases
class BackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-backend-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'backend.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/backend_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.backend_run.argtypes=[c.c_uint,c.POINTER(c.c_uint8),c.POINTER(c.c_char_p),c.POINTER(c.c_uint32),c.c_uint,c.c_uint,c.c_uint,c.c_uint,c.c_int,c.c_int];cls.native.backend_run.restype=None
        cls.native.backend_error_text.restype=c.c_char_p;cls.native.backend_sent.restype=c.c_char_p
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.raw=(ROOT/'tests/fixtures/backend-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.output=(ROOT/'tests/fixtures/backend-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def test_original_inputs_and_provenance(self):
        self.assertEqual(self.inputs,backend_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256']);self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256']);self.assertEqual(self.raw[4:8],bytes(4));self.assertEqual(self.output[4:8],bytes(4))
        for address in ('0x400d44cc','0x400d8834','0x400d8770','0x400d5b5c','0x400d76e0','0x400d4534'):self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_backend_matches_original(self):
        ops=['send','sync','complete','poll','sequence'];self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                b=(c.c_uint8*0x440).from_buffer_copy(bytes.fromhex(row['data']));strings=(c.c_char_p*5)(*[row[k].encode('latin1') for k in ('action','key','value','uid','response')]);items=(c.c_uint32*len(row['items']))(*row['items'])
                self.native.backend_run(ops.index(row['op']),b,strings,items,len(items),row['command'],row['now'],row['tick'],row['connect'],row['connected'])
                self.assertEqual(bytes(b).hex(),expected['data']);self.assertEqual(self.native.backend_error_text().decode(),expected['error']);self.assertEqual(self.native.backend_sent().decode(),expected['sent']);self.assertEqual([self.native.controller_event(i).decode() for i in range(self.native.controller_event_count())],expected['events']);self.assertEqual(self.native.controller_assertions(),expected['assertion_count']);self.assertEqual(self.native.controller_clock(),expected['clock'])
if __name__=='__main__':unittest.main()
