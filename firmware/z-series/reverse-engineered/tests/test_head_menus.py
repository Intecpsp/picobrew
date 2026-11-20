"""Original Head application menus through real display composition."""
import gzip
import hashlib
import json
import tempfile
import unittest
import head_menus_cases as inputs
import head_menus_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadMenusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-menus-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-menus-original.json.gz").read_bytes()))

    def test_full_framebuffer_and_widget_state(self):
        cases = inputs.cases()
        self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))

    def test_original_instructions_and_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-menus-validation.json").read_text())
        compressed = (ROOT / "tests/fixtures/head-menus-original.json.gz").read_bytes()
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

    def test_full_application_entry_and_shared_display_alias(self):
        addresses={item['address']for item in self.fixture['original_functions']}
        self.assertTrue({'0x401080d4','0x40102d30','0x40107e20','0x401065b4','0x401045dc','0x40107f6c','0x401037d4'}.issubset(addresses))
        self.assertEqual(len(self.fixture['cases']),167)
        self.assertGreater(self.fixture['modeled_boundaries'].get('SDK_WiFi_String',0),0)

if __name__=='__main__':unittest.main()
