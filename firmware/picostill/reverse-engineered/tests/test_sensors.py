"""Board calibration and I2C effects compared with independent OEM execution."""
import ctypes as c
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
import sensor_cases


class SensorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="picostill-sensors-");cls.addClassCleanup(cls.temp.cleanup)
        output=Path(cls.temp.name)/"sensors.so"
        subprocess.run(shlex.split(os.environ.get("CXX","c++"))+["-std=c++11","-O2","-Wall","-Wextra","-Werror","-pedantic",
            "-fno-fast-math","-ffp-contract=off","-fPIC","-shared","-I",str(ROOT/"include"),str(ROOT/"tests/sensors_bridge.cpp")]+
            [str(ROOT/"src"/name) for name in ("core.cpp","enums.cpp","sensors.cpp")]+["-o",str(output)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_uint32)
        cls.native.sensor_run.argtypes=[c.c_uint,p,c.POINTER(c.c_int),c.c_uint];cls.native.sensor_run.restype=None
        cls.native.sensor_convert.argtypes=[c.c_uint,c.c_int32];cls.native.sensor_convert.restype=c.c_double
        cls.native.sensor_parameters.argtypes=[c.c_uint,p];cls.native.sensor_parameters.restype=c.c_int
        for name,args,result in (("result_count",[],c.c_uint),("result",[c.c_uint],c.c_uint32),("event_count",[],c.c_uint),
                                ("event",[c.c_uint],c.c_char_p),("assertions",[],c.c_uint32)):
            f=getattr(cls.native,"sensor_"+name);f.argtypes=args;f.restype=result
        cls.input_bytes=(ROOT/"tests/fixtures/sensors-inputs.json.gz").read_bytes();cls.inputs=json.loads(gzip.decompress(cls.input_bytes))
        cls.output_bytes=(ROOT/"tests/fixtures/sensors-original.json.gz").read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output_bytes))

    def test_calibration_bits_match_original_static_initializer_writes(self):
        for channel,words in enumerate(self.fixture["calibration_words"]):
            actual=(c.c_uint32*9)();self.assertEqual(self.native.sensor_parameters(channel,actual),1);self.assertEqual(list(actual),words)
        for invalid in (4,0xffffffff):self.assertEqual(self.native.sensor_parameters(invalid,(c.c_uint32*9)()),0)

    def test_fixture_identity_and_original_initialization_are_verified(self):
        raw=(ROOT.parent/"oem/upstream/picostill_0_0_35.bin").read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.fixture["input_sha256"])
        self.assertEqual(hashlib.sha256(self.input_bytes).hexdigest(),self.fixture["cases_input_gzip_sha256"])
        self.assertEqual(self.inputs,sensor_cases.cases());self.assertEqual(len(self.inputs),len(self.fixture["cases"]))
        self.assertEqual(self.input_bytes[4:8],b"\0"*4);self.assertEqual(self.output_bytes[4:8],b"\0"*4)
        self.assertEqual(self.fixture["original_functions_executed"]["0x400d8688"],len(self.inputs))

    def test_acquisition_bytes_temperature_math_and_metric_state_match_original(self):
        operations={"adc":0,"pressure":1,"temperatures":2,"pressure_sample":3}
        for row,expected in zip(self.inputs,self.fixture["cases"]):
            with self.subTest(id=row["id"],op=row["op"]):
                self.assertEqual(row["id"],expected["id"])
                if row["op"]=="convert":
                    actual=self.native.sensor_convert(row["channel"],row["adc"])
                    golden=struct.unpack(">d",bytes.fromhex(expected["double_bits"]))[0]
                    if math.isnan(golden):self.assertTrue(math.isnan(actual))
                    elif math.isfinite(golden):self.assertAlmostEqual(actual,golden,delta=2e-12)
                    else:self.assertEqual(actual,golden)
                else:
                    words=[row.get("three",0),row.get("status",0x40),row.get("range",25),row["returned"],row["available"]]
                    words+=row.get("counts",[0,0])+row.get("values",[0]*5)+row.get("rates",[0]*5)+row.get("averages",[0]*5)
                    self.native.sensor_run(operations[row["op"]],(c.c_uint32*len(words))(*words),(c.c_int*len(row["bytes"]))( *row["bytes"]),len(row["bytes"]))
                    self.assertEqual([self.native.sensor_result(i) for i in range(self.native.sensor_result_count())],expected["words"])
                self.assertEqual([self.native.sensor_event(i).decode() for i in range(self.native.sensor_event_count())],expected["events"])
                self.assertEqual(self.native.sensor_assertions(),expected["assertion_count"])


if __name__=="__main__":unittest.main()
