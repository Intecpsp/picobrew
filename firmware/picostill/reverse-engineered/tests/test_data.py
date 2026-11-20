"""Original-execution comparisons for identity and credential application wiring."""
import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import data_cases
class DataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-data-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'data.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/data_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.data_run.argtypes=[c.c_uint]+[c.POINTER(c.c_uint8)]*3+[c.c_char_p,c.c_char_p,c.c_uint,c.c_uint,c.POINTER(c.c_uint8)];cls.native.data_run.restype=c.c_uint
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.raw=(ROOT/'tests/fixtures/data-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.output=(ROOT/'tests/fixtures/data-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def test_original_inputs_and_provenance(self):
        self.assertEqual(self.inputs,data_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256']);self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(self.raw[4:8],bytes(4));self.assertEqual(self.output[4:8],bytes(4))
        for address in ('0x400d50e8','0x400d4860','0x400d4984','0x400d4a10','0x400d48b0','0x400d4678'):self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_identity_and_configure_match_original(self):
        ops=['init','digit','copy','hex','uid','ssid','password','configure','report'];self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                data=(c.c_uint8*208).from_buffer_copy(bytes.fromhex(row['data']));stored=(c.c_uint8*100)(*[204]*100);out=(c.c_uint8*128)(*[204]*128);raw=(c.c_uint8*max(1,len(row['raw'])//2)).from_buffer_copy(bytes.fromhex(row['raw']) or b'\0')
                result=self.native.data_run(ops.index(row['op']),data,stored,out,row['text'].encode('latin1'),row['other'].encode('latin1'),row['number'],row['capacity'],raw)
                self.assertEqual(bytes(data).hex(),expected['data']);self.assertEqual(bytes(stored).hex(),expected['stored']);self.assertEqual(bytes(out).hex(),expected['output']);self.assertEqual(result,expected['changed'] if row['op']=='configure' else expected['result'])
                self.assertEqual([self.native.controller_event(i).decode('latin1') for i in range(self.native.controller_event_count())],expected['events']);self.assertEqual(self.native.controller_assertions(),expected['assertion_count']);self.assertEqual(self.native.controller_clock(),expected['clock'])
if __name__=='__main__':unittest.main()
