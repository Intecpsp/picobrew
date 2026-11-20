"""Concrete original menu composition, owned entries and marquee timing."""
import gzip
import hashlib
import json
import tempfile
import unittest
import head_menu_cases as inputs
import head_menu_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-menu-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-menu-original.json.gz").read_bytes()))

    def test_full_framebuffer_and_widget_state(self):
        cases = inputs.cases()
        self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))

    def test_original_instructions_and_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-menu-validation.json").read_text())
        compressed = (ROOT / "tests/fixtures/head-menu-original.json.gz").read_bytes()
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

    def test_returning_assertions_and_actual_marquee_generation(self):
        lines = {case["state"][27] for case in self.fixture["cases"]}
        self.assertTrue({0x236, 0x3bf}.issubset(lines))
        addresses = {item["address"] for item in self.fixture["original_functions"]}
        self.assertTrue({"0x400dfa7c", "0x400e79f8", "0x400f86c8", "0x400db014"}.issubset(addresses))
        self.assertGreater(self.fixture["modeled_boundaries"]["windowed_ABI_MOVSP_stack_move"], 0)
        self.assertGreater(self.fixture["modeled_boundaries"]["assertion_delay_200ms_prescribed_clock"], 0)

    def test_owned_recipe_payload_and_explicit_service_boundaries(self):
        self.assertEqual(self.native.head_menu_ownership_and_guards(), 0)


if __name__ == "__main__":
    unittest.main()
