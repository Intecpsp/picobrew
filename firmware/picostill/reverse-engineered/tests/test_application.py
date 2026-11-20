"""Top-level state transitions, queue ordering and nested controller execution."""
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
import application_cases
class ApplicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picostill-app-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'app.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off',
            '-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/application_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.application_run.argtypes=[c.c_uint,c.POINTER(c.c_uint8),c.c_uint32,c.c_uint32,c.c_uint32,c.c_int];cls.native.application_run.restype=None
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.input_bytes=(ROOT/'tests/fixtures/application-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.input_bytes))
        cls.output_bytes=(ROOT/'tests/fixtures/application-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output_bytes))
    def test_identity_and_original_nested_execution(self):
        self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(hashlib.sha256(self.input_bytes).hexdigest(),self.fixture['cases_input_gzip_sha256']);self.assertEqual(self.inputs,application_cases.cases())
        self.assertEqual(len(self.inputs),len(self.fixture['cases']));self.assertEqual(self.input_bytes[4:8],b'\0'*4);self.assertEqual(self.output_bytes[4:8],b'\0'*4)
        for address in ('0x400d8e64','0x400d95cc','0x400d903c','0x400d9120','0x400d6cf4','0x400d5dbc','0x400d4c98','0x400d446c','0x400d4678'):
            self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def test_application_state_and_effects_match_original(self):
        operations=dict(queue_button=0,queue_wifi=1,state=2,button=3,wifi=4,menu=5,request_program=6,request_stop=7,poll=8)
        for row,golden in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                raw=bytes.fromhex(row['data']);data=(c.c_uint8*len(raw)).from_buffer_copy(raw)
                self.native.application_run(operations[row['op']],data,row['arg'],row['arg2'],row['now'],row['high'])
                expected=bytes.fromhex(golden['data']);difference=[hex(i) for i,(a,b) in enumerate(zip(bytes(data),expected)) if a!=b]
                self.assertFalse(difference,'Original object byte differences: '+', '.join(difference[:30]))
                self.assertEqual([self.native.controller_event(i).decode() for i in range(self.native.controller_event_count())],golden['events'])
                self.assertEqual(self.native.controller_assertions(),golden['assertion_count']);self.assertEqual(self.native.controller_clock(),golden['clock'])
if __name__=='__main__':unittest.main()
