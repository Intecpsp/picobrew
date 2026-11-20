import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import http_cases,extract_http_assets
class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picoferm-http-');cls.addClassCleanup(cls.temp.cleanup);output=Path(cls.temp.name)/'http.so'
        command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/http_bridge.cpp')]
        subprocess.run(command+[str(ROOT/'src'/f'{name}.cpp') for name in ['core','hardware','data','http','http_server']]+['-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));cls.native.ferm_http_run.argtypes=[c.c_uint,c.POINTER(c.c_uint32),c.c_char_p,c.c_char_p,c.c_char_p,c.c_char_p,c.c_uint,c.POINTER(c.c_uint8)];cls.native.ferm_http_run.restype=c.c_uint
        cls.native.ferm_http_field.argtypes=[c.c_uint];cls.native.ferm_http_field.restype=c.c_char_p
        cls.native.ferm_http_event_count.restype=c.c_uint;cls.native.ferm_http_event.argtypes=[c.c_uint];cls.native.ferm_http_event.restype=c.c_char_p
        cls.raw=(ROOT/'tests/fixtures/http-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw))
        cls.fixture=json.loads(gzip.decompress(Path(os.environ.get('PICOFERM_HTTP_FIXTURE',ROOT/'tests/fixtures/http-original.json.gz')).read_bytes()))
    def run_native(self,row):
        words=(c.c_uint32*20)(*row['words']);wire=row['wire'].encode('latin1');record=(c.c_uint8*236).from_buffer_copy(bytes.fromhex(row['record']))
        result=self.native.ferm_http_run(row['op'],words,row['first'].encode('latin1'),row['second'].encode('latin1'),row['body'].encode('latin1'),wire,len(wire),record)
        actual=dict(id=row['id'],result=result,content_length=words[0],headers_complete=words[1],changed=words[2],now=words[3],consumed=words[4],events=[self.native.ferm_http_event(i).decode('latin1') for i in range(self.native.ferm_http_event_count())])
        for index,name in enumerate(['uri','body','error','form','trigger','decoded','response','record','data']):actual[name]=self.native.ferm_http_field(index).decode('latin1')
        return actual
    def test_original_identity_assets_and_complete_helpers(self):
        self.assertEqual(self.inputs,http_cases.cases());self.assertEqual(self.fixture['cases_input_gzip_sha256'],hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(self.fixture['input_sha256'],hashlib.sha256((ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes()).hexdigest())
        for address in ['0x40202d74','0x40202d8c','0x402034e4','0x40204b2c','0x40201050','0x402044e8','0x402079c0','0x402035d0','0x40204cf8']:
            self.assertGreater(self.fixture['original_functions_executed'][address],0)
        for path,content in extract_http_assets.products().items():self.assertEqual((ROOT/path).read_bytes(),content)
    def test_complete_requests_pages_effects_and_owned_buffers_match_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            actual=self.run_native(row)
            for field,value in expected.items():
                with self.subTest(id=row['id'],field=field):self.assertEqual(actual[field],value)
    def test_explicit_bounded_rejections_are_not_claimed_as_oem_successes(self):
        row=dict(self.inputs[0],id='owned-uri-rejection',op=4,first='GET /'+'x'*256+' HTTP/1.0')
        actual=self.run_native(row);self.assertEqual(actual['error'],'Request URI too long');self.assertEqual(bytes.fromhex(actual['uri']).split(b'\0',1)[0],b'previous-uri')
        row=dict(self.inputs[0],id='owned-body-rejection',op=5,wire='POST /x HTTP/1.0\nContent-Length: 512\n\n'+'b'*512)
        actual=self.run_native(row);self.assertEqual(actual['error'],'Request body too long');self.assertEqual(bytes.fromhex(actual['body']),b'b'*511+b'\0');self.assertEqual(actual['headers_complete'],1)
        row=dict(self.inputs[0],id='owned-zero-capacity-rejection',op=3,first='x',words=list(self.inputs[0]['words']));row['words'][15]=0
        actual=self.run_native(row);self.assertEqual(actual['decoded'],'a5'*128);self.assertEqual(actual['events'],['yield','yield'])
if __name__=='__main__':unittest.main()
