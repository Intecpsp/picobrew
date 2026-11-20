"""Concrete Head widget pixels and original assertion continuations."""
import gzip
import hashlib
import json
import tempfile
import unittest
import head_widgets_cases as inputs
import head_widgets_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadWidgetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-widgets-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-widgets-original.json.gz").read_bytes()))

    def test_full_framebuffer_and_widget_state(self):
        cases = inputs.cases()
        self.assertEqual(len(cases), len(self.fixture["cases"]))
        self.assertEqual({case["operation"] for case in cases}, set(range(8)))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))

    def test_original_instructions_and_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-widgets-validation.json").read_text())
        compressed = (ROOT / "tests/fixtures/head-widgets-original.json.gz").read_bytes()
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        self.assertEqual(hashlib.sha256(compressed).hexdigest(), proof["compressed_fixture_sha256"])
        self.assertEqual(hashlib.sha256(gzip.decompress(compressed)).hexdigest(), proof["fixture_sha256"])
        self.assertEqual(proof["cases"], len(inputs.cases()))
        self.assertTrue(proof["native_validation"]["all_results_match"])
        for group in ["generator_files", "validated_source_sha256"]:
            for path, expected in proof[group].items():
                self.assertEqual(common.shared.digest(ROOT / path), expected, path)
        raw = common.INPUT.read_bytes()
        parsed = common.images.parse_image(raw)
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                    for s in parsed["segments"]]
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])

    def test_returning_assertions_and_dynamic_text_stack(self):
        seen = set()
        for case, original in zip(inputs.cases(), self.fixture["cases"]):
            line = original["state"][1]
            if line:
                seen.add(line)
                self.assertGreater(original["state"][2], case["counter"])
                if case["operation"] in [3, 6]:
                    self.assertEqual(original["state"][0], 1)
        self.assertTrue({0x9b3, 0x8e0, 0x614, 0x3bf, 0x103}.issubset(seen))
        self.assertGreater(self.fixture["modeled_boundaries"]["windowed_ABI_MOVSP_stack_move"], 0)
        self.assertGreater(self.fixture["modeled_boundaries"]["assertion_delay_200ms_prescribed_clock"], 0)


if __name__ == "__main__":
    unittest.main()
