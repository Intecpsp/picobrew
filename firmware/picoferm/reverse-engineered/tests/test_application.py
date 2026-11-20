import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import application_cases
class ApplicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picoferm-application-');cls.addClassCleanup(cls.temp.cleanup);output=Path(cls.temp.name)/'application.so'
        command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/application_bridge.cpp')]
        subprocess.run(command+[str(p)for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_uint8);w=c.POINTER(c.c_uint32);s=c.c_char_p;cls.native.ferm_application_run.argtypes=[s,p,p,w,w,w,c.c_uint,w,c.c_uint,w,c.c_uint,s,s,s];cls.native.ferm_application_run.restype=c.c_uint
        cls.native.ferm_event_count.restype=c.c_uint;cls.native.ferm_event.argtypes=[c.c_uint];cls.native.ferm_event.restype=c.c_char_p
        for name in ['label','wifi_error','backend_error','sent']:getattr(cls.native,'ferm_application_'+name).restype=c.c_char_p
        for name in ['clock','position','reboot']:getattr(cls.native,'ferm_application_'+name).restype=c.c_uint
        cls.raw=(ROOT/'tests/fixtures/application-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/application-original.json.gz').read_bytes()))
    def test_original_identity_and_continuations(self):
        self.assertEqual(self.inputs,application_cases.cases());self.assertEqual(self.fixture['cases_input_gzip_sha256'],hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(self.fixture['input_sha256'],hashlib.sha256((ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes()).hexdigest())
        for address in ['0x40202d74','0x40202d8c','0x40206bf8','0x40207ba8','0x40203014','0x402039cc','0x4020686c','0x40207878','0x40205e08','0x40203210']:
            self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_application_state_effects_and_complete_storage_match_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                data=(c.c_uint8*0xaf0).from_buffer_copy(bytes.fromhex(row['data']));storage=(c.c_uint8*512).from_buffer_copy(bytes.fromhex(row['storage']));items=(c.c_uint32*len(row['items']))(*row['items']);connections=(c.c_uint32*len(row['connect']))(*row['connect']);reads=(c.c_uint32*len(row['reads']))(*row['reads']);wifi=(c.c_uint32*18)(*row['wifi'])
                params=(c.c_uint32*11)(*[row[x]for x in ['now','tick','analog','gpio','storage_size','update','dns','arg','arg2','polls','connected']])
                result=self.native.ferm_application_run(row['op'].encode(),data,storage,params,wifi,reads,len(reads),items,len(items),connections,len(connections),*[row[x].encode()for x in ['wifi_error','backend_error','update_error']])
                actual=dict(id=row['id'],data=bytes(data).hex(),storage=bytes(storage).hex(),result=result,label=self.native.ferm_application_label().decode('latin1'),wifi_error=self.native.ferm_application_wifi_error().decode('latin1'),backend_error=self.native.ferm_application_backend_error().decode('latin1'),sent=self.native.ferm_application_sent().decode('latin1'),clock=self.native.ferm_application_clock(),position=self.native.ferm_application_position(),update_reboot=self.native.ferm_application_reboot(),events=[self.native.ferm_event(i).decode('latin1')for i in range(self.native.ferm_event_count())])
                for key in actual:
                    if actual[key]!=expected[key]:
                        if key in ['data','storage']:
                            a=bytes.fromhex(actual[key]);b=bytes.fromhex(expected[key]);difference=[(hex(i),x,y)for i,(x,y)in enumerate(zip(a,b))if x!=y];self.fail(key+' differences '+str(difference[:35]))
                        else:self.assertEqual(actual[key],expected[key],key)
if __name__=='__main__':unittest.main()
