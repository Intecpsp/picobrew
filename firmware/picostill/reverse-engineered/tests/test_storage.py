"""Compare NVS contents, allocation failure continuations and credential records."""
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
import storage_cases
class StorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-storage-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'storage.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/storage_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.storage_run.argtypes=[c.c_uint,c.c_uint,c.c_int,c.c_uint,c.c_int,c.c_int,c.c_uint]+[c.c_int]*6;cls.native.storage_run.restype=None
        for name,args,result in (('value',[c.c_uint],c.c_uint),('bytes',[c.c_uint],c.c_char_p),('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p)):
            fn=getattr(cls.native,'storage_'+name);fn.argtypes=args;fn.restype=result
        for name in ('assertions','clock'):getattr(cls.native,'controller_'+name).restype=c.c_uint32
        cls.raw=(ROOT/'tests/fixtures/storage-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw))
        cls.output=(ROOT/'tests/fixtures/storage-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def test_input_and_original_execution(self):
        self.assertEqual(self.inputs,storage_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256'])
        self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(self.raw[4:8],b'\0'*4);self.assertEqual(self.output[4:8],b'\0'*4)
        for a in ('0x400d3350','0x400d4130','0x400d4830','0x400d3534','0x400d4678'):self.assertGreater(self.fixture['original_functions_executed'][a],0)
    def test_storage_matches_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,golden in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                self.native.storage_run(('begin','initialize','load','save').index(row['op']),*[row[k] for k in ('requested','blob','size','present','dirty','version','opened','query','read','write','expand','allocation')])
                self.assertEqual([self.native.storage_value(i) for i in range(5)],golden['values'])
                self.assertEqual([self.native.storage_bytes(i).decode() for i in range(3)],golden['bytes'])
                self.assertEqual([self.native.storage_event(i).decode() for i in range(self.native.storage_event_count())],golden['events'])
                self.assertEqual(self.native.controller_assertions(),golden['assertion_count']);self.assertEqual(self.native.controller_clock(),golden['clock'])
if __name__=='__main__':unittest.main()
