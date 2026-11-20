from __future__ import annotations
import ctypes
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_safeties as validation


class SafetiesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="zseries-safeties-test-")
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/safeties-original.json").read_text())

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_latches_deadlines_and_filter_reads(self):
        for item in self.fixture["cases"]:
            with self.subTest(case=item["input"]["id"]):
                validation.compare_case(self.native, item["input"], item["original"])

    def test_missing_gpio_does_not_mutate_or_read(self):
        item = validation.case("missing", values={"hex": 90}, words={"hot": 1, "hex_rise_tripped": 1})
        values = (ctypes.c_double * 7)(*item["values"])
        words = (ctypes.c_uint32 * 21)(*item["words"])
        count = ctypes.c_uint32(10)
        self.assertEqual(self.native.safeties_action(values, words, ctypes.byref(count), 1), 0)
        self.assertEqual(list(values), item["values"])
        self.assertEqual(list(words), item["words"])
        self.assertEqual(count.value, 0)

    def test_interlocks_reach_runtime_pause_priority(self):
        executable = Path(self.build.name) / "safeties-runtime"
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        sources = ["tests/safeties_runtime_integration.cpp", "src/core_control_state.cpp", "src/safeties.cpp",
                   "src/program_runtime_heater.cpp", "src/program_runtime.cpp", "src/program_runtime_execution.cpp",
                   "src/runtime_instruction.cpp", "src/quantity.cpp", "src/heater.cpp", "src/msp_protocol.c"]
        subprocess.run(compiler + ["-std=c++11", "-O2", "-fno-fast-math", "-ffp-contract=off", "-I", str(ROOT / "include")]
                       + [str(ROOT / source) for source in sources] + ["-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

    def test_provenance(self):
        report = json.loads((ROOT / "analysis/safeties-validation.json").read_text())
        self.assertEqual(report["input_sha256"], validation.digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin"))
        self.assertEqual(report["portable_golden_sha256"], validation.digest(ROOT / "tests/fixtures/safeties-original.json"))
        self.assertEqual(report["portable_golden_cases"], len(self.fixture["cases"]))
        for path, expected in report["source_sha256"].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        self.assertEqual(report["exact_binary64_matches"], report["binary64_comparisons"])
        self.assertTrue(report["all_results_match"])
        self.assertFalse(report["hardware_tested"])


if __name__ == "__main__":
    unittest.main()
