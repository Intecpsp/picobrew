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
            result, events = validation.invoke(self.native, item["boiler"])
            self.assertEqual(result, 0)
            self.assertEqual(events, item["events"])

    def test_missing_components_cannot_report_success(self):
        result, events = validation.invoke(self.native, missing=True)
        self.assertEqual((result, events), (1, []))
        for stage in range(22):
            result, events = validation.invoke(self.native, fail_stage=stage)
            self.assertEqual(result, 2)
            self.assertEqual(events[-1], [0, stage, 0, 0, 0])

    def test_timer_failures_do_not_enable_interrupts(self):
        for failure, expected_result in ((-2, 6), (-3, 7)):
            result, events = validation.invoke(self.native, fail_stage=failure)
            self.assertEqual(result, expected_result)
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
