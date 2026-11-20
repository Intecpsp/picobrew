"""Compare original route effects, complete HTTP bytes and assembled browser pages."""
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
import http_server_cases
class HttpServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-server-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'server.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/http_server_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.server_run.argtypes=[c.c_char_p,c.POINTER(c.c_uint32),c.POINTER(c.c_char_p),c.c_int,c.POINTER(c.c_uint8),c.c_uint32,c.c_uint];cls.native.server_run.restype=None
        cls.native.server_bytes.argtypes=[c.c_uint];cls.native.server_bytes.restype=c.c_char_p;cls.native.server_error.restype=c.c_char_p;cls.native.server_output.restype=c.c_char_p
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.raw=(ROOT/'tests/fixtures/http-server-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.output=(ROOT/'tests/fixtures/http-server-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def run_case(self,row):
        flags=(c.c_uint32*len(row['flags']))(*row['flags']);values=[s.encode('latin1') for s in row['strings']+row['ssids']];strings=(c.c_char_p*len(values))(*values)
        raw=bytes.fromhex(row['factory']);factory=(c.c_uint8*len(raw)).from_buffer_copy(raw);self.native.server_run(row['request'].encode('latin1'),flags,strings,row['scan'],factory,row['now'],row['tick'])
    def test_input_and_original_execution(self):
        self.assertEqual(self.inputs,http_server_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256'])
        self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(self.raw[4:8],b'\0'*4);self.assertEqual(self.output[4:8],b'\0'*4)
        for a in ('0x400d780c','0x400d35b0','0x400d4dc8','0x400d3bf8','0x400d3c80','0x400d3a18','0x400d3d1c'):self.assertGreater(self.fixture['original_functions_executed'][a],0)
        self.assertEqual(self.fixture['loop_control_probe'],dict(original_entry='0x400d5754',before_byte=165,cleared_bytes=804,backedges=199))
    def test_responses_and_effects_match_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,golden in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                self.run_case(row);self.assertEqual([self.native.server_bytes(i).decode() for i in range(3)],golden['bytes']);self.assertEqual(self.native.server_error().decode(),golden['error'])
                actual=self.native.server_output().decode('latin1');self.assertEqual(hashlib.sha256(actual.encode('latin1')).hexdigest(),hashlib.sha256(golden['output'].encode('latin1')).hexdigest(),'Complete response bytes differ')
                self.assertEqual([self.native.controller_event(i).decode('latin1') for i in range(self.native.controller_event_count())],golden['events'])
                self.assertEqual(self.native.controller_assertions(),golden['assertion_count']);self.assertEqual(self.native.controller_clock(),golden['clock'])
if __name__=='__main__':unittest.main()
