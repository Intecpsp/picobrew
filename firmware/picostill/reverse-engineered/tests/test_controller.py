"""Controller/lifecycle state and external effects versus original instructions."""
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
import controller_cases

class ControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picostill-controller-');cls.addClassCleanup(cls.temp.cleanup)
        output=Path(cls.temp.name)/'controller.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic',
            '-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/controller_bridge.cpp')]+
            [str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_int)
        cls.native.controller_run.argtypes=[c.c_uint,c.POINTER(c.c_uint8),c.c_uint64,c.c_uint32,c.c_int,p,c.c_uint,p,c.c_uint]
        cls.native.controller_run.restype=None
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('assertions',[],c.c_uint32),('clock',[],c.c_uint32),('flag_count',[],c.c_uint),('flag',[c.c_uint],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.input_bytes=(ROOT/'tests/fixtures/controller-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.input_bytes))
        cls.output_bytes=(ROOT/'tests/fixtures/controller-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output_bytes))

    def test_fixture_identity_and_original_integration_execution(self):
        self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(hashlib.sha256(self.input_bytes).hexdigest(),self.fixture['cases_input_gzip_sha256'])
        self.assertEqual(self.inputs,controller_cases.cases());self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        self.assertEqual(self.input_bytes[4:8],b'\0'*4);self.assertEqual(self.output_bytes[4:8],b'\0'*4)
        for address in ('0x400d6a08','0x400d6cf4','0x400d71e8','0x400d7288','0x400d4678','0x400d2730','0x400d68c4','0x400d4074'):
            self.assertGreater(self.fixture['original_functions_executed'][address],0)

    def test_full_original_object_state_and_observable_effects_match(self):
        operations=dict(enable=0,enable_internal=1,enable_record=2,capture=3,hardware_error=4,hardware_poll=5,pin_test=6,connection_test=7,state=8,start=9,stop=10,poll=11,flags=12)
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                original=bytes.fromhex(row['data']);data=(c.c_uint8*len(original)).from_buffer_copy(original)
                self.native.controller_run(operations[row['op']],data,row['arg'],row['now'],row['high'],
                    (c.c_int*len(row['pressure']))(*row['pressure']),len(row['pressure']),(c.c_int*len(row['adc']))(*row['adc']),len(row['adc']))
                golden=bytes.fromhex(expected['data']);actual=bytes(data)
                differences=[hex(i) for i,(a,b) in enumerate(zip(actual,golden)) if a!=b]
                self.assertFalse(differences,'Original object byte differences: '+', '.join(differences[:30]))
                self.assertEqual([self.native.controller_event(i).decode() for i in range(self.native.controller_event_count())],expected['events'])
                self.assertEqual(self.native.controller_assertions(),expected['assertion_count'])
                self.assertEqual(self.native.controller_clock(),expected['clock'])
                self.assertEqual([self.native.controller_flag(i) for i in range(self.native.controller_flag_count())],expected['flags'])

if __name__=='__main__':unittest.main()
