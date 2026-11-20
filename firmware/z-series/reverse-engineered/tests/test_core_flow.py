from __future__ import annotations
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_core_flow as validation


class CoreFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="zseries-core-flow-test-")
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/core-flow-original.json").read_text())

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_core_state_and_ordered_effects(self):
        for item in self.fixture["cases"]:
            with self.subTest(case=item["input"]["id"]):
                validation.compare_case(self.native, item["input"], item["original"])

    def test_tick_rejects_missing_component_before_mutation(self):
        item = validation.case("missing", state=0, queue_mask=63)
        status, result = validation.native_case(self.native, item, missing=True)
        self.assertEqual(status, 1)
        self.assertEqual(result["words"], item["words"])
        self.assertEqual(result["events"], [])

    def test_button_becomes_a_command_for_the_next_pass(self):
        item = validation.case("button", queue_mask=8)
        status, result = validation.native_case(self.native, item)
        self.assertEqual(status, 0)
        self.assertEqual(result["words"][validation.WORDS.index("queue_mask")], 4)
        self.assertEqual(result["words"][validation.WORDS.index("command_id")], 1)
        self.assertTrue(all(event[0] == 100 for event in result["events"]))
        item["words"] = result["words"]
        _, next_result = validation.native_case(self.native, item)
        self.assertEqual(next_result["words"][validation.WORDS.index("queue_mask")], 0)
        self.assertEqual(next_result["events"][-1], [24, 1, 0, 0])

    def test_provenance(self):
        report = json.loads((ROOT / "analysis/core-flow-validation.json").read_text())
        self.assertEqual(report["input_sha256"], validation.digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin"))
        self.assertEqual(report["portable_golden_sha256"], validation.digest(ROOT / "tests/fixtures/core-flow-original.json"))
        self.assertEqual(report["portable_golden_cases"], len(self.fixture["cases"]))
        for path, expected in report["source_sha256"].items():
            self.assertEqual(validation.digest(ROOT / path), expected, path)
        self.assertTrue(report["all_results_match"])
        self.assertFalse(report["hardware_tested"])


if __name__ == "__main__":
    unittest.main()
