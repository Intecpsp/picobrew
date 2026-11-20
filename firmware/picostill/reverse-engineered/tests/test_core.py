"""Compile owned source and compare results/effects with original instructions."""
import ctypes as c
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

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import analyze_images
import core_cases
import recover_enums
import recover_web
from esp_image import mapped_offset, parse_esp32


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="picostill-core-")
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / "core.so"
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run(compiler+["-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                                "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared", "-I", str(ROOT / "include"),
                                str(ROOT / "tests/core_bridge.cpp"), str(ROOT / "src/core.cpp"),
                                str(ROOT / "src/enums.cpp"), "-o", str(library)], check=True, capture_output=True, text=True)
        cls.native = c.CDLL(str(library))
        p = c.POINTER(c.c_uint32)
        for name, result, args in (
            ("reset", None, [c.c_uint32]), ("assertion_count", c.c_uint32, []),
            ("event_count", c.c_uint, []), ("event", c.c_char_p, [c.c_uint]),
            ("ema", c.c_double, [c.c_double, c.c_int32, c.c_double]),
            ("thermistor", c.c_double, [c.POINTER(c.c_double), c.c_int32]),
            ("sensors", None, [p, p, p]), ("fan", None, [p, p]), ("pump", c.c_uint32, [p]),
            ("errors", None, [c.c_uint32, c.c_uint32, p]), ("button", None, [p, p, c.c_uint, p]),
            ("enum", c.c_char_p, [c.c_uint, c.c_uint])):
            function = getattr(cls.native, "native_"+name)
            function.restype = result
            function.argtypes = args
        cls.inputs_bytes = (ROOT / "tests/fixtures/core-inputs.json").read_bytes()
        cls.inputs = json.loads(cls.inputs_bytes)
        cls.fixture = json.loads((ROOT / "tests/fixtures/core-original.json").read_text())

    def test_fixture_is_bound_to_original_image_and_reproducible_inputs(self):
        raw = (ROOT.parent / "oem/upstream/picostill_0_0_35.bin").read_bytes()
        self.assertEqual(self.fixture["input_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(self.fixture["cases_input_sha256"], hashlib.sha256(self.inputs_bytes).hexdigest())
        self.assertEqual(self.inputs, core_cases.cases())
        self.assertEqual(len(self.fixture["cases"]), len(self.inputs))
        self.assertGreater(self.fixture["original_functions_executed"]["0x400d4678"], 0)
        self.assertGreater(self.fixture["modeled_boundaries"]["Arduino_delay"], 0)
        self.assertNotIn("returning_assertion", self.fixture["modeled_boundaries"])

    def test_core_result_memory_and_effects_match_original_execution(self):
        u32 = lambda values: (c.c_uint32*len(values))(*values)
        for row, expected in zip(self.inputs, self.fixture["cases"]):
            with self.subTest(id=row["id"], op=row["op"]):
                self.assertEqual(row["id"], expected["id"])
                self.native.native_reset(row.get("assertion_seed", 0))
                op = row["op"]
                out = (c.c_uint32*4)()
                results = []
                actual_double = None
                if op == "ema":
                    actual_double = self.native.native_ema(row["previous"], row["count"], row["sample"])
                elif op == "thermistor":
                    actual_double = self.native.native_thermistor((c.c_double*5)(*row["parameters"]), row["adc"])
                elif op == "sensors":
                    self.native.native_sensors(u32(row["counts"]), u32(row["bits"]), out)
                    results = list(out)[:3]
                elif op == "fan":
                    self.native.native_fan(u32(sum(row["fans"], [])), out)
                    results = list(out)[:2]
                elif op == "pump":
                    results = [self.native.native_pump(u32(row["bits"]))]
                elif op == "errors":
                    self.native.native_errors(row["error"], row["existing"], out)
                    results = list(out)[:2]
                elif op == "button":
                    self.native.native_button(u32(row["words"]), u32(sum(row["steps"], [])), len(row["steps"]), out)
                    results = list(out)
                else:
                    self.fail("Unimplemented test operation "+op)
                self.assertEqual(results, expected["results"])
                self.assertEqual(self.native.native_assertion_count(), expected["assertion_count"])
                events = [self.native.native_event(i).decode() for i in range(self.native.native_event_count())]
                self.assertEqual(events, expected["events"])
                if actual_double is not None:
                    golden = struct.unpack(">d", bytes.fromhex(expected["double_bits"]))[0]
                    if math.isnan(golden):
                        self.assertTrue(math.isnan(actual_double))
                    elif op == "thermistor" and math.isfinite(golden):
                        # Explicit Java Math.log / host libm boundary: numeric,
                        # not bit-identical libm implementation verification.
                        self.assertAlmostEqual(actual_double, golden, delta=2e-12)
                    else:
                        self.assertEqual(actual_double, golden)

    def test_all_enum_labels_and_invalid_bounds_match_original_tables(self):
        catalog = recover_enums.validated_catalog()
        checked = 0
        for index, enum in enumerate(catalog["enums"]):
            for value in enum["values"]:
                self.assertEqual(self.native.native_enum(index, value["value"]).decode(), value["text"])
                checked += 1
            for invalid in (enum["count"], enum["count"]+1, 0x7fffffff, 0xffffffff):
                self.assertEqual(self.native.native_enum(index, invalid).decode(), "<%s::INVALID>" % enum["name"])
        self.assertEqual(checked, 111)
        for name, contents in recover_enums.outputs(catalog).items():
            self.assertEqual((ROOT / name).read_text(), contents)

    def test_image_inventory_and_analysis_mapping_are_current(self):
        for name, content in analyze_images.reports().items():
            self.assertEqual(json.loads((ROOT / "analysis" / name).read_text()), content)

    def test_extracted_browser_assets_are_exact_oem_bytes(self):
        for name, data in recover_web.outputs().items():
            self.assertEqual((ROOT / name).read_bytes(), data)

    def test_recorded_source_entries_and_replay_provenance_are_current(self):
        report = json.loads((ROOT / "analysis/source-evidence.json").read_text())
        raw = (ROOT.parent / "oem/upstream/picostill_0_0_35.bin").read_bytes()
        image = parse_esp32(raw)
        self.assertEqual(report["input_sha256"], image["sha256"])
        self.assertEqual(len(report["functions"]), 159)
        for function in report["functions"]:
            for region in function["ranges"]:
                offset = mapped_offset(image, region["start"], region["size_bytes"])
                self.assertEqual(hashlib.sha256(raw[offset:offset+region["size_bytes"]]).hexdigest(), region["sha256"])
        for name, expected in report["files"].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), expected, name)


if __name__ == "__main__":
    unittest.main()
