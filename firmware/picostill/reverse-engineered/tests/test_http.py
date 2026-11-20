"""HTTP parser/form execution and explicit owned bounds guards."""
import ctypes as c
import gzip
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import http_cases
class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-http-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'http.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/http_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.http_run.argtypes=[c.c_uint,c.c_char_p,c.c_char_p,c.c_uint,c.c_uint32,c.c_uint,c.c_int,c.c_char_p,c.POINTER(c.c_int),c.c_uint,c.c_uint];cls.native.http_run.restype=None
        cls.native.http_bytes.argtypes=[c.c_uint];cls.native.http_bytes.restype=c.c_char_p;cls.native.http_error.restype=c.c_char_p;cls.native.http_value.restype=c.c_uint
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.raw=(ROOT/'tests/fixtures/http-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.output=(ROOT/'tests/fixtures/http-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def run_case(self,row):
        items=(c.c_int*len(row['items']))(*row['items'])
        self.native.http_run(('parse','request','form','decode','boolean','equal','prefix').index(row['op']),row['text'].encode('latin1'),row['other'].encode('latin1'),row['capacity'],row['now'],row['tick'],row['keep'],row['error'].encode(),items,len(items),row['timeout'])
    def test_input_and_original_execution(self):
        self.assertEqual(self.inputs,http_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256'])
        self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(self.raw[4:8],b'\0'*4);self.assertEqual(self.output[4:8],b'\0'*4)
        for a in ('0x400d35b0','0x400d2bf0','0x400d4dc8','0x400d4d20','0x400d4678'):self.assertGreater(self.fixture['original_functions_executed'][a],0)
    def test_http_matches_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,golden in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                self.run_case(row);self.assertEqual([self.native.http_bytes(i).decode() for i in range(4)],golden['bytes'])
                self.assertEqual(self.native.http_error().decode(),golden['error']);self.assertEqual(self.native.http_value(),golden['result'])
                self.assertEqual([self.native.controller_event(i).decode('latin1') for i in range(self.native.controller_event_count())],golden['events'])
                self.assertEqual(self.native.controller_assertions(),golden['assertion_count']);self.assertEqual(self.native.controller_clock(),golden['clock'])
    def test_owned_bounds_reject_unsafe_domains(self):
        row=dict(self.inputs[0]);row.update(op='request',text='GET /'+'x'*255+' HTTP/1.1');self.run_case(row)
        self.assertEqual(self.native.http_error(),b'Request-URI exceeds buffer')
        row.update(op='form',text='x'*512);self.run_case(row);self.assertEqual(self.native.http_error(),b'Unterminated FORM data')
if __name__=='__main__':unittest.main()
