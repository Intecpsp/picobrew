"""OEM localized data, real instruction lookup behavior and asset identity."""
import base64
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

import run_msp_emulation as common
import ui_catalog_cases as inputs

ROOT = common.COMPONENT


class UiCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-ui-catalog-")
        cls.addClassCleanup(cls.temporary.cleanup)
        library = Path(cls.temporary.name) / "catalog.so"
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                       "-fPIC", "-shared", "-I" + str(ROOT / "include"), str(ROOT / "src/ui_catalog.cpp"),
                       str(ROOT / "tests/ui_catalog_bridge.cpp"), "-o", str(library)], check=True)
        cls.native = ctypes.CDLL(str(library))
        cls.native.ui_catalog_label.argtypes = [ctypes.c_uint] * 3
        cls.native.ui_catalog_label.restype = ctypes.c_char_p
        cls.native.ui_catalog_count.argtypes = [ctypes.c_uint]
        cls.native.ui_catalog_count.restype = ctypes.c_uint
        cls.native.ui_catalog_screen.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
        cls.native.ui_catalog_screen.restype = ctypes.c_uint
        cls.native.ui_catalog_bitmap.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]
        cls.native.ui_catalog_bitmap.restype = ctypes.c_void_p
        fixture = Path(os.environ.get("Z_UI_CATALOG_FIXTURE", ROOT / "tests/fixtures/ui-catalog-original.json"))
        cls.fixture = json.loads(fixture.read_text())

    def test_all_original_label_and_screen_lookups(self):
        cases = inputs.cases()
        self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                expected = base64.b64decode(original["text_base64"])
                if case["kind"] == "label":
                    self.assertEqual(self.native.ui_catalog_label(case["category"], case["item"], case["language"]), expected)
                else:
                    text = ctypes.c_void_p(0x01020304)
                    result = self.native.ui_catalog_screen(case["item"], case["language"], ctypes.byref(text))
                    if original["changed"]:
                        self.assertEqual(result, 0)
                        self.assertEqual(ctypes.string_at(text), expected)
                    else:
                        self.assertEqual(result, 1)
                        self.assertEqual(text.value, 0x01020304)

    def test_bitmap_descriptors_and_pixels_are_exact_original_assets(self):
        _, _, bitmaps, _ = inputs.catalog.recover()
        for index, bitmap in enumerate(bitmaps):
            fields = (ctypes.c_uint * 8)()
            pointer = self.native.ui_catalog_bitmap(index, fields)
            self.assertIsNotNone(pointer)
            self.assertEqual(list(fields), [bitmap["width"], bitmap["height"], bitmap["row_bytes"], 0, 0,
                                           bitmap["width"] - 1, bitmap["height"] - 1, bitmap["pixels_bytes"]])
            self.assertEqual(ctypes.string_at(pointer, fields[7]), bitmap["pixels"])
        self.assertIsNone(self.native.ui_catalog_bitmap(16, (ctypes.c_uint * 8)()))

    def test_native_guards_preserve_caller_output(self):
        for index, item in enumerate(inputs.catalog.TEXT):
            self.assertEqual(self.native.ui_catalog_count(index), item[4])
        self.assertEqual(self.native.ui_catalog_count(18), 0)
        self.assertEqual(self.native.ui_catalog_label(18, 0, 0), b"<UiTextCategory::INVALID>")
        for language in [3, 0x7fffffff, 0xffffffff]:
            text = ctypes.c_void_p(0x01020304)
            self.assertEqual(self.native.ui_catalog_screen(1, language, ctypes.byref(text)), 2)
            self.assertEqual(text.value, 0x01020304)

    def test_generated_source_and_original_instruction_provenance(self):
        source, report = inputs.catalog.outputs()
        self.assertEqual((ROOT / "src/ui_catalog.cpp").read_text(), source)
        self.assertEqual((ROOT / "analysis/ui-assets.json").read_text(), report)
        if "Z_UI_CATALOG_FIXTURE" in os.environ:
            self.skipTest("Development fixture has not yet been accepted")
        proof = json.loads((ROOT / "analysis/ui-catalog-validation.json").read_text())
        self.assertEqual(common.shared.digest(ROOT / "tests/fixtures/ui-catalog-original.json"), proof["fixture_sha256"])
        for group in ["generator_files", "validated_source_sha256"]:
            for filename, expected in proof[group].items():
                self.assertEqual(common.shared.digest(ROOT / filename), expected, filename)
        raw = common.INPUT.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), self.fixture["input_sha256"])
        parsed = common.images.parse_image(raw)
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"]) for s in parsed["segments"]]
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])


if __name__ == "__main__":
    unittest.main()
