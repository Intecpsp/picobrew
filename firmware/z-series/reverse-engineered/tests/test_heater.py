"""Original instruction fixtures, actual initialized data, and owned boundaries."""
from __future__ import annotations
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import analyze_images
import validate_heater as validation


class HeaterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="zseries-heater-test-")
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/heater-original.json").read_text())

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_state_and_gpio_sequences(self):
        for item in self.fixture["cases"]:
            with self.subTest(case=item["input"]["id"]):
                validation.compare_case(self.native, item["input"],
                                        {"id": item["input"]["id"], "snapshots": item["snapshots"]})

    def test_original_thermal_limits(self):
        original = (ROOT.parent / "oem/upstream/zseries_0_0_119.bin").read_bytes()
        mapping = analyze_images.parse_image(original)

        def read(address, size):
            for segment in mapping["segments"]:
                if segment["load_address"] <= address and address + size <= segment["load_address"] + segment["size_bytes"]:
                    offset = segment["file_offset"] + address - segment["load_address"]
                    return original[offset:offset + size]
            self.fail("Original initialized memory range is missing")

        self.assertEqual(struct.unpack("<4I", read(0x3ffbdc48, 16)), (4000, 4000, 0xffffffff, 0xffffffff))
        self.assertEqual(struct.unpack("<8d", read(0x3ffbdc58, 64)), (0., 110., 0., 105., 0., 105., -20., 62.))

    def test_missing_io_rejected_before_mutation(self):
        for kind, args in (("initialize_outputs", []), ("enable", [1]), ("stop", []), ("tick20", []), ("tick1", [])):
            item = validation.case(kind, [], words={"started": 1, "automatic": 1})
            result, values, words, gpio = validation.invoke_native(self.native, validation.command(kind, *args),
                                                                  item["values"], item["words"], missing_io=True)
            self.assertEqual(result, 0)
            self.assertEqual(values, item["values"])
            self.assertEqual(words, item["words"])
            self.assertEqual(gpio, [])

    def test_owned_invalid_numeric_boundaries(self):
        for action in (validation.command("tunings", math.inf, 1., 1.),
                       validation.command("tunings", 1., math.nan, 1.),
                       validation.command("limits", 0., math.inf),
                       validation.command("target", math.nan),
                       validation.command("configure_boiler", 2)):
            item = validation.case("boundary", [])
            result, values, words, gpio = validation.invoke_native(self.native, action, item["values"], item["words"])
            self.assertEqual(result, 0)
            self.assertEqual(values, item["values"])
            self.assertEqual(words, item["words"])
            self.assertEqual(gpio, [])
        for sample in (0, 0x80000000):
            item = validation.case("sample-time", [], words={"sample_time_ms": sample})
            result, values, words, _ = validation.invoke_native(self.native, validation.command("tunings", 1, 1, 1),
                                                               item["values"], item["words"])
            self.assertEqual((result, values, words), (0, item["values"], item["words"]))

    def test_fixture_and_source_provenance(self):
        report = json.loads((ROOT / "analysis/heater-validation.json").read_text())
        self.assertEqual(self.fixture["input_sha256"], validation.IMAGE_SHA)
        self.assertEqual(report["input_sha256"], validation.digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin"))
        self.assertEqual(report["portable_golden_sha256"], validation.digest(ROOT / "tests/fixtures/heater-original.json"))
        self.assertEqual(report["portable_golden_cases"], len(self.fixture["cases"]))
        self.assertEqual(self.fixture["values"], validation.VALUE_NAMES)
        self.assertEqual(self.fixture["words"], validation.WORD_NAMES)
        for path, expected in report["source_sha256"].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        self.assertTrue(report["all_results_match"])
        self.assertFalse(report["hardware_tested"])


if __name__ == "__main__":
    unittest.main()
