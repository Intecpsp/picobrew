"""Original Wi-Fi transitions, network calls, events and timeout boundaries."""
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
import wifi_cases
class WifiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-wifi-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'wifi.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/wifi_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.wifi_run.argtypes=[c.c_uint,c.POINTER(c.c_uint8),c.c_uint32,c.c_uint32,c.c_int,c.POINTER(c.c_uint32)];cls.native.wifi_run.restype=None;cls.native.wifi_label.restype=c.c_char_p
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.raw=(ROOT/'tests/fixtures/wifi-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.output=(ROOT/'tests/fixtures/wifi-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def test_input_and_original_execution(self):
        self.assertEqual(self.inputs,wifi_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256'])
        self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(self.raw[4:8],b'\0'*4);self.assertEqual(self.output[4:8],b'\0'*4)
        for a in ('0x400d5fa4','0x400d63b0','0x400d638c','0x400d27d8','0x400d4678'):self.assertGreater(self.fixture['original_functions_executed'][a],0)
    def test_wifi_matches_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,golden in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                raw=bytes.fromhex(row['data']);data=(c.c_uint8*len(raw)).from_buffer_copy(raw);network=(c.c_uint32*len(row['network']))(*row['network'])
                self.native.wifi_run(('state','poll','fail','label').index(row['op']),data,row['arg'],row['now'],row['empty_ap'],network)
                self.assertEqual(bytes(data).hex(),golden['data']);self.assertEqual(self.native.wifi_label().decode(),golden['label'])
                self.assertEqual([self.native.controller_event(i).decode() for i in range(self.native.controller_event_count())],golden['events'])
                self.assertEqual(self.native.controller_assertions(),golden['assertion_count']);self.assertEqual(self.native.controller_clock(),golden['clock'])
if __name__=='__main__':unittest.main()
