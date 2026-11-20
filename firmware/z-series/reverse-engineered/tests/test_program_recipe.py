"""Historical recipe observations: 497 complete outputs and 13 assertion boundaries."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import program_recipe_cases as inputs
import program_recipe_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class ProgramRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-recipes-native-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = native.build(cls.temporary.name)
        fixture = Path(os.environ.get("Z_RECIPE_FIXTURE", ROOT / "tests/fixtures/program-recipe-original.json"))
        cls.fixture = json.loads(fixture.read_text())
        cls.cases = inputs.cases()
        if cls.fixture["mode"] == "smoke":
            cls.cases = cls.cases[:20] + [c for c in cls.cases if c["action"] == 2][:10] + [c for c in cls.cases if c["action"] == 3][:10]
        cls.actual = native.run(cls.executable, cls.cases)

    def test_historical_complete_and_first_assertion_recipe_observations(self):
        self.assertEqual(len(self.cases), len(self.fixture["cases"]))
        self.assertEqual(len(self.cases), len(self.actual))
        for index, (case, original, actual) in enumerate(zip(self.cases, self.fixture["cases"], self.actual)):
            if case["action"]:
                with self.subTest(case=index, action=case["action"]):
                    self.assertEqual(actual, original)

    def test_original_json_errors_capacity_and_explicit_library_differences(self):
        for index, (case, original, actual) in enumerate(zip(self.cases, self.fixture["cases"], self.actual)):
            if case["action"] == 0:
                with self.subTest(case=index, input=case["json"][:80]):
                    self.assertEqual(actual, native.expected(case, original, ctypes.sizeof(ctypes.c_long)))

    def test_owned_strings_runtime_load_and_distinct_failure_state(self):
        subprocess.run([str(self.executable), "--integration"], check=True)

    def test_original_source_dependency_and_32bit_provenance(self):
        if self.fixture["mode"] == "smoke":
            self.skipTest("Development smoke is not committed evidence")
        fixture = ROOT / "tests/fixtures/program-recipe-original.json"
        report = json.loads((ROOT / "analysis/program-recipe-validation.json").read_text())
        fixture_sha = hashlib.sha256(fixture.read_bytes()).hexdigest()
        self.assertEqual(report["fixture_sha256"], fixture_sha)
        self.assertEqual(report["cases"], len(self.cases))
        for group in ("generator_files", "validated_source_sha256"):
            for filename, expected in report[group].items():
                self.assertEqual(hashlib.sha256((ROOT / filename).read_bytes()).hexdigest(), expected, filename)
        raw = common.INPUT.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), report["input_sha256"])
        parsed = common.images.parse_image(raw)
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                    for s in parsed["segments"]]
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])
        for site in report["instruction_boundaries"][0]["addresses"]:
            self.assertEqual(common.read_mapped(raw, segments, int(site, 16), 3).hex(), "00409a")
        width = json.loads((ROOT / "analysis/program-recipe-32bit-validation.json").read_text())
        self.assertEqual(width["fixture_sha256"], fixture_sha)
        self.assertEqual(width["cases"], len(self.cases))
        self.assertEqual(width["abi"], dict(pointer=4, long=4, json_integer=4, local_capacity=1024, http_capacity=4096))
        self.assertTrue(width["all_application_cases_match_historical_observations"])
        self.assertEqual((width["complete_application_matches"], width["first_assertion_observations"]), (497, 13))
        self.assertTrue(width["runtime_integration_passed"])
        for filename, expected in width["source_sha256"].items():
            self.assertEqual(hashlib.sha256((ROOT / filename).read_bytes()).hexdigest(), expected, filename)
        build = json.loads((ROOT / "analysis/program-recipe-target-build.json").read_text())
        self.assertIn("5.2.0", build["compiler_version"])
        self.assertEqual(build["dependency_pin_sha256"], hashlib.sha256((ROOT / "dependencies/arduinojson.json").read_bytes()).hexdigest())
        for item in build["objects"]:
            self.assertEqual(hashlib.sha256((ROOT / item["source"]).read_bytes()).hexdigest(), item["source_sha256"])


if __name__ == "__main__":
    unittest.main()
