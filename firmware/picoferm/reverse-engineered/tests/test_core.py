"""Compare packed application state and effects to independent OEM execution."""
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
import core_cases


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='picoferm-core-');cls.addClassCleanup(cls.temp.cleanup)
        output=Path(cls.temp.name)/'core.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic',
            '-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/core_bridge.cpp'),
            str(ROOT/'src/core.cpp'),'-o',str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_uint32)
        cls.native.ferm_run.argtypes=[c.c_uint,c.POINTER(c.c_uint8),p,c.c_char_p];cls.native.ferm_run.restype=None
        for name,args,result in (('result',[c.c_uint],c.c_uint32),('message',[],c.c_char_p),('event_count',[],c.c_uint),
                                ('event',[c.c_uint],c.c_char_p),('measurement',[c.c_uint,p],c.c_int)):
            fn=getattr(cls.native,'ferm_'+name);fn.argtypes=args;fn.restype=result
        cls.input_bytes=(ROOT/'tests/fixtures/core-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.input_bytes))
        cls.output_bytes=(ROOT/'tests/fixtures/core-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output_bytes))

    def test_original_identity_and_actual_assertion_execution(self):
        raw=(ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.fixture['input_sha256'])
        self.assertEqual(hashlib.sha256(self.input_bytes).hexdigest(),self.fixture['cases_input_gzip_sha256'])
        self.assertEqual(self.inputs,core_cases.cases());self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        self.assertEqual(self.input_bytes[4:8],b'\0'*4);self.assertEqual(self.output_bytes[4:8],b'\0'*4)
        functions=self.fixture['original_functions_executed']
        for address in ('0x40202d74','0x40202d8c','0x40210554','0x40105b4c','0x40105cb0'):
            self.assertGreater(functions[address],0)
        self.assertEqual(self.fixture['cases'][0]['events'],['yield','yield'])
        self.assertEqual(self.fixture['cases'][1]['events'],['yield','yield'])

    def test_complete_storage_records_and_application_effects_match_original(self):
        operations=dict(assertion=0,warning=1,session=2,reset=3,migrate=4,sanitize=5,calibration=6,append=7,iterator=8,button=9,persist=10,adc=11)
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id'],op=row['op']):
                data=(c.c_uint8*236).from_buffer_copy(bytes.fromhex(row.get('data','00'*236)))
                words=[row.get('gpio',1),row.get('temperature',row.get('processed',0)),row.get('raw_pressure',row.get('pressure',row.get('index',0))),row.get('machine_state',0),row.get('session_state',0)]
                response=row.get('response','').encode('latin1')
                self.native.ferm_run(operations[row['op']],data,(c.c_uint32*5)(*words),response)
                actual=dict(id=row['id'],events=[self.native.ferm_event(i).decode() for i in range(self.native.ferm_event_count())])
                if 'data' in row:actual['data']=bytes(data).hex()
                result=self.native.ferm_result
                if row['op']=='session':actual.update(state=result(0),error_flags=result(1))
                elif row['op']=='calibration':actual.update(error=result(0),can_calibrate=result(1),message=self.native.ferm_message().decode())
                elif row['op']=='iterator':actual.update(done=result(0),processed=result(1),index=result(2))
                elif row['op']=='button':actual['button']=result(0)
                self.assertEqual(actual,expected)

    def test_owned_guards_are_explicitly_outside_original_returning_domain(self):
        self.native.ferm_run(2,None,(c.c_uint32*5)(1,0,0,0,0),None)
        self.assertEqual(self.native.ferm_result(2),0)
        self.assertEqual([self.native.ferm_event(i).decode() for i in range(self.native.ferm_event_count())],['yield','yield'])
        for index in (12,255,0xffffffff):self.assertEqual(self.native.ferm_measurement(index,(c.c_uint32*2)()),0)
        for index in (0,11):self.assertEqual(self.native.ferm_measurement(index,(c.c_uint32*2)()),1)

    def test_source_evidence_matches_current_reviewed_files(self):
        evidence=json.loads((ROOT/'analysis/source-evidence.json').read_text())
        self.assertEqual(len(evidence['functions']),111)
        for relative,expected in evidence['files'].items():
            self.assertEqual(hashlib.sha256((ROOT/relative).read_bytes()).hexdigest(),expected,relative)
        self.assertFalse(evidence['reconstruction_complete']);self.assertTrue(evidence['complete_application_linked']);self.assertTrue(evidence['application_reconstruction_implemented'])


if __name__=='__main__':unittest.main()
