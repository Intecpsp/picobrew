"""Keep original returning-assertion behavior distinct from bounded rejections."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_program_recipe_continuations as validation


class ProgramRecipeContinuationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads(validation.FIXTURE.read_text())
        cls.report = json.loads(validation.REPORT.read_text())

    def test_actual_original_returns_and_completes_program_generation(self):
        for index, case, original in zip(validation.INDICES, validation.cases(), self.fixture["cases"]):
            with self.subTest(historical_index=index):
                self.assertEqual(original["assertion_counter"], 1)
                self.assertEqual(original["assertion_delays"], [200])
                self.assertEqual(len(original["assertion_lines"]), 1)
                self.assertEqual(original["buffer"]["code"][-1], [40, 0])
                self.assertEqual(len(original["buffer"]["code"]), 7)
                self.assertEqual(len(original["buffer"]["strings"]), 2)
                self.assertGreater(original["stats"]["duration"], 0)
                self.assertEqual(original["stats"]["pressure"], "40f8bcd000000000")
                self.assertEqual(original["stats"]["maximum"], "4058800000000000")
                if case["action"] == 1:
                    self.assertEqual((original["backend_state"], original["notifications"]), (7, 1))
                else:
                    self.assertEqual((original["backend_state"], original["notifications"]), (0x5555, 0))
                if index == 351:
                    self.assertEqual(original["stats"]["rate"], "405c666666666666")
                    self.assertEqual(original["stats"]["duration"], 1962)
                    self.assertEqual(original["parameters"]["water"], ["0000000000000000", 0])
                elif original["assertion"] == 626:
                    self.assertEqual(original["buffer"]["name"], "x" * 47)
                else:
                    self.assertIn("", original["buffer"]["strings"])

    def test_native_bounded_rejections_are_documented_deviations(self):
        with tempfile.TemporaryDirectory(prefix="z-recipe-bounded-policy-") as directory:
            actual = validation.native.run(validation.native.build(directory), validation.cases())
        differences = validation.compare_bounded_policy(self.fixture, actual)
        self.assertEqual(differences, self.report["intentional_bounded_policy_deviations"])
        self.assertEqual(len(differences), 13)
        self.assertEqual(self.report["exact_final_state_matches"], 0)

    def test_original_ranges_and_source_identity(self):
        validation.verify_original(self.fixture)
        self.assertEqual(self.report["fixture_sha256"], validation.digest(validation.FIXTURE))
        self.assertEqual(self.report["historical_fixture_sha256"], validation.digest(validation.HISTORICAL))
        for path, identity in self.report["source_sha256"].items():
            self.assertEqual(identity, validation.digest(ROOT / path), path)
        for path, identity in self.report["original_execution_provenance"]["generator_files"].items():
            self.assertEqual(identity, validation.digest(ROOT / path), path)


if __name__ == "__main__":
    unittest.main()
