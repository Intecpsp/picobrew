from __future__ import annotations
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_core_startup as validation


class CoreStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="zseries-core-test-")
        cls.native, _ = validation.load_native(cls.build.name)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_startup_order(self):
        fixture = json.loads((ROOT / "tests/fixtures/core-startup-original.json").read_text())
        for item in fixture["cases"]:
            result, events = validation.invoke(self.native, item["boiler"], fail_stage=-1 if item["timer_start"] else -2)
            self.assertEqual(result, 0)
            self.assertEqual(events, item["events"])

    def test_missing_components_cannot_report_success(self):
        result, events = validation.invoke(self.native, missing=True)
        self.assertEqual((result, events), (1, []))
        for stage in range(22):
            result, events = validation.invoke(self.native, fail_stage=stage)
            self.assertEqual(result, 2)
            self.assertEqual(events[-1], [0, stage, 0, 0, 0])

    def test_software_timer_assertion_returns_and_hardware_setup_continues(self):
        result, events = validation.invoke(self.native, fail_stage=-2)
        self.assertEqual(result, 0)
        self.assertIn([14, 0x557, 0, 0, 0], events)
        self.assertIn([15, 200, 0, 0, 0], events)
        self.assertEqual(events[-1], [0, 21, 0, 0, 0])
        self.assertTrue(any(event[0] == 12 for event in events))

    def test_owned_missing_hardware_timer_guard(self):
        result, events = validation.invoke(self.native, fail_stage=-3)
        self.assertEqual(result, 7)
        self.assertFalse(any(event[0] == 12 for event in events))

    def test_provenance(self):
        report = json.loads((ROOT / "analysis/core-startup-validation.json").read_text())
        self.assertEqual(report["input_sha256"], validation.digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin"))
        self.assertEqual(report["portable_golden_sha256"], validation.digest(ROOT / "tests/fixtures/core-startup-original.json"))
        for path, expected in report["source_sha256"].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        self.assertTrue(report["all_results_match"])
        self.assertFalse(report["hardware_tested"])


if __name__ == "__main__":
    unittest.main()
