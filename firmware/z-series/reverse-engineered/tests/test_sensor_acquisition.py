from __future__ import annotations
import ctypes
import json
import math
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_acquisition as validation


class AcquisitionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="zseries-acquisition-test-")
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/acquisition-original.json").read_text())

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_transactions_and_smoother_states(self):
        for item in self.fixture["cases"]:
            with self.subTest(case=item["input"]["id"]):
                validation.compare_case(self.native, item["input"], item["original"])

    def test_missing_transport_and_invalid_boiler_do_not_mutate(self):
        for item in validation.input_cases()[:5]:
            result, output = validation.run_adc(self.native, item, missing_io=True)
            self.assertEqual(result, 1)
            self.assertEqual(output, {"types": item["types"], "raw": item["raw"], "enabled": item["enabled"], "events": []})
        item = dict(validation.input_cases()[0], boiler=2)
        result, output = validation.run_adc(self.native, item)
        self.assertEqual(result, 2)
        self.assertEqual(output["events"], [])
        self.assertEqual(output["raw"], item["raw"])
        self.assertEqual(output["types"], item["types"])

    def test_failed_byte_read_stops_before_publication(self):
        item = {"kind": "poll", "address": 52, "enabled": 1, "types": [0, 1, 1, 2], "raw": [123] * 4,
                "request_count": 8, "bytes": [0, 15, -1, 0, 0, 20, 0, 30]}
        result, output = validation.run_adc(self.native, item)
        self.assertEqual(result, 4)
        self.assertEqual(output["raw"], [15, -1, -1, -1])
        self.assertEqual(len(output["events"]), 5)

    def test_owned_smoother_boundaries(self):
        for count, next_index, value, expected in ((11, 0, 20., 7), (0, 10, 20., 7),
                                                  (0, 0, math.nan, 6), (0, 0, math.inf, 6)):
            values = (ctypes.c_double * 12)(*([0.] * 12))
            words = (ctypes.c_uint32 * 4)(next_index, count, 0, 0)
            self.assertEqual(self.native.smoother_action(values, words, value, 1), expected)
            self.assertEqual(list(values), [0.] * 12)
            self.assertEqual(list(words), [next_index, count, 0, 0])

    def test_acquisition_to_heater_stale_input_integration(self):
        executable = Path(self.build.name) / "sensor-heater"
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run(compiler + ["-std=c++11", "-O2", "-fno-fast-math", "-ffp-contract=off", "-I", str(ROOT / "include"),
                       str(ROOT / "tests/sensor_heater_integration.cpp"), str(ROOT / "src/sensor_acquisition.cpp"),
                       str(ROOT / "src/temperature.cpp"), str(ROOT / "src/heater.cpp"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

    def test_provenance(self):
        report = json.loads((ROOT / "analysis/acquisition-validation.json").read_text())
        self.assertEqual(report["input_sha256"], validation.digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin"))
        self.assertEqual(report["portable_golden_sha256"], validation.digest(ROOT / "tests/fixtures/acquisition-original.json"))
        self.assertEqual(report["portable_golden_cases"], len(self.fixture["cases"]))
        for path, expected in report["source_sha256"].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        self.assertTrue(report["all_results_match"])
        self.assertFalse(report["hardware_tested"])


if __name__ == "__main__":
    unittest.main()
