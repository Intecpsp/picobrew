from __future__ import annotations
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_auxiliary_sensors as validation


class AuxiliarySensorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="zseries-auxiliary-test-")
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/auxiliary-sensors-original.json").read_text())

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_sensor_transactions_and_compensation(self):
        for item in self.fixture["cases"]:
            with self.subTest(case=item["input"]["id"]):
                validation.compare_case(self.native, item["input"], item["original"])

    def test_preserved_pressure_byte_assembly_defect(self):
        results = []
        for low_byte in range(256):
            item = validation.case("ignored-third-byte", "tick_bmp")
            item["replies"]["247"]["bytes"][2] = low_byte
            item["replies"]["250"]["bytes"][2] = low_byte
            status, result = validation.native_case(self.native, item)
            self.assertEqual(status, 0)
            results.append((result["words"][5:8], result["values"][3:]))
        self.assertTrue(all(result == results[0] for result in results))
        self.assertEqual(results[0][0][0], (101 << 12) | (90 << 4) | (101 >> 4))

    def test_original_failed_reads_reprocess_previous_values(self):
        item = validation.case("failed-reads", "tick_bmp")
        item["ends"] = [1, 1]
        status, result = validation.native_case(self.native, item)
        self.assertEqual(status, 2)
        self.assertEqual(result["words"][5:7], item["words"][5:7])
        self.assertEqual(result["words"][8], item["words"][9])
        self.assertNotEqual(result["values"][3], item["values"][3])
        self.assertFalse(any(event[0] in (4, 5) for event in result["events"]))

    def test_missing_transport_has_no_effect(self):
        for kind in validation.KINDS:
            with self.subTest(kind=kind):
                item = validation.case("missing", kind)
                status, result = validation.native_case(self.native, item, missing=True)
                self.assertEqual(status, 1)
                self.assertEqual(result["words"], item["words"])
                self.assertEqual(result["values"], item["values"])
                self.assertEqual(result["events"], [])

    def test_provenance(self):
        report = json.loads((ROOT / "analysis/auxiliary-sensors-validation.json").read_text())
        self.assertEqual(report["input_sha256"], validation.digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin"))
        self.assertEqual(report["portable_golden_sha256"], validation.digest(ROOT / "tests/fixtures/auxiliary-sensors-original.json"))
        self.assertEqual(report["portable_golden_cases"], len(self.fixture["cases"]))
        for path, expected in report["source_sha256"].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        self.assertEqual(report["exact_binary64_matches"], report["binary64_comparisons"])
        self.assertTrue(report["all_results_match"])
        self.assertFalse(report["hardware_tested"])


if __name__ == "__main__":
    unittest.main()
