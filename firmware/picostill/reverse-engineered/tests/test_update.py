"""Original URL parsing, HTTP headers and firmware-update state comparison."""
import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import update_cases
class UpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-update-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'update.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/update_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.update_run.argtypes=[c.c_uint,c.POINTER(c.c_uint32),c.c_char_p,c.POINTER(c.c_char_p),c.c_uint,c.POINTER(c.c_uint8),c.c_uint,c.POINTER(c.c_uint8),c.POINTER(c.c_uint8)];cls.native.update_run.restype=None;cls.native.update_sent.restype=c.c_char_p
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.raw=(ROOT/'tests/fixtures/update-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.output=(ROOT/'tests/fixtures/update-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def test_original_inputs_and_provenance(self):
        self.assertEqual(self.inputs,update_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256']);self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256']);self.assertEqual(self.raw[4:8],bytes(4));self.assertEqual(self.output[4:8],bytes(4))
        for address in ('0x400d5e1c','0x400d37a4','0x400d8880','0x400d91a8','0x400d28c8'):self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_update_matches_original(self):
        ops=['url','begin','poll','sequence'];self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                args=(c.c_uint32*14)(*[row[k] for k in ('state','length','binary','written','now','changed','connect','begin','end','finished','error','read_limit','write_limit','polls')]);lines=(c.c_char_p*len(row['lines']))(*[s.encode() for s in row['lines']]);raw=bytes.fromhex(row['body']);body=(c.c_uint8*max(1,len(raw))).from_buffer_copy(raw or b'\0');buffer=(c.c_uint8*2048)();params=(c.c_uint8*145)()
                self.native.update_run(ops.index(row['op']),args,row['uri'].encode(),lines,len(lines),body,len(raw),buffer,params)
                self.assertEqual(list(args[:5]),expected['fields']);self.assertEqual(bytes(buffer).hex(),expected['buffer']);self.assertEqual(bytes(params).hex(),expected['parameters']);self.assertEqual(self.native.update_sent().decode(),expected['sent']);self.assertEqual([self.native.controller_event(i).decode() for i in range(self.native.controller_event_count())],expected['events']);self.assertEqual(self.native.controller_assertions(),expected['assertion_count']);self.assertEqual(self.native.controller_clock(),expected['clock'])
if __name__=='__main__':unittest.main()
