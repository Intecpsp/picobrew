"""Independent original-instruction comparisons for actuator and pump-test source."""
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
sys.path.insert(0,str(ROOT/"tools"))
import hardware_cases
import recover_animations


class HardwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="picostill-hardware-");cls.addClassCleanup(cls.temp.cleanup)
        output=Path(cls.temp.name)/"hardware.so"
        subprocess.run(shlex.split(os.environ.get("CXX","c++"))+["-std=c++11","-O2","-Wall","-Wextra","-Werror","-pedantic",
            "-fno-fast-math","-ffp-contract=off","-fPIC","-shared","-I",str(ROOT/"include"),
            str(ROOT/"tests/hardware_bridge.cpp")]+[str(ROOT/"src"/name) for name in ("core.cpp","enums.cpp","hardware.cpp","animations.cpp")]+["-o",str(output)],
            check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(output));p=c.POINTER(c.c_uint32)
        cls.native.hardware_run.argtypes=[c.c_uint,p,p,c.c_uint,c.c_char_p];cls.native.hardware_run.restype=None
        for name,arguments,result in (("result_count",[],c.c_uint),("result",[c.c_uint],c.c_uint32),
            ("event_count",[],c.c_uint),("event",[c.c_uint],c.c_char_p),("assertions",[],c.c_uint32)):
            f=getattr(cls.native,"hardware_"+name);f.argtypes=arguments;f.restype=result
        cls.input_bytes=(ROOT/"tests/fixtures/hardware-inputs.json.gz").read_bytes()
        cls.inputs=json.loads(gzip.decompress(cls.input_bytes))
        cls.output_bytes=(ROOT/"tests/fixtures/hardware-original.json.gz").read_bytes()
        cls.fixture=json.loads(gzip.decompress(cls.output_bytes))

    def test_fixture_provenance_and_lossless_deterministic_encoding(self):
        raw=(ROOT.parent/"oem/upstream/picostill_0_0_35.bin").read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.fixture["input_sha256"])
        self.assertEqual(hashlib.sha256(self.input_bytes).hexdigest(),self.fixture["cases_input_gzip_sha256"])
        self.assertEqual(self.input_bytes[4:8],b"\0"*4);self.assertEqual(self.output_bytes[4:8],b"\0"*4)
        self.assertEqual(self.inputs,hardware_cases.cases())
        self.assertEqual(len(self.inputs),len(self.fixture["cases"]))
        self.assertGreater(self.fixture["original_functions_executed"]["0x400d4678"],0)
        self.assertGreater(self.fixture["modeled_boundaries"]["Arduino_delay"],0)

    def test_results_and_gpio_pwm_diagnostic_effects_match_original(self):
        encode=lambda values:(c.c_uint32*len(values))(*values)
        operations={"output":0,"fan":1,"fanbank":2,"led":3,"pump_test":4}
        for row,expected in zip(self.inputs,self.fixture["cases"]):
            with self.subTest(id=row["id"],op=row["op"]):
                self.assertEqual(row["id"],expected["id"])
                words=row.get("words",[row.get("initial",0)])
                if row["op"]=="pump_test":words=[row["state"]]+row["started"]+row["vacuum"]+row["deadline"]+row["pressures"]+[row["error"]]
                text=row.get("text");self.native.hardware_run(operations[row["op"]],encode(words),encode(sum(row["steps"],[])),len(row["steps"]),None if text is None else text.encode())
                actual=[self.native.hardware_result(i) for i in range(self.native.hardware_result_count())]
                self.assertEqual(actual,expected["words"])
                events=[self.native.hardware_event(i).decode() for i in range(self.native.hardware_event_count())]
                self.assertEqual(events,expected["events"])
                self.assertEqual(self.native.hardware_assertions(),expected["assertion_count"])

    def test_animation_table_is_extracted_from_original_bytes(self):
        for name,text in recover_animations.outputs().items():self.assertEqual((ROOT/name).read_text(),text)


if __name__=="__main__":unittest.main()
