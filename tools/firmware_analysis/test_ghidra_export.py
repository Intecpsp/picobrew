"""Offline checks for the image/loader boundary; no Ghidra download required."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ghidra_export as export


class LayoutTests(unittest.TestCase):
    def setUp(self):
        self.data = bytes(range(128))
        self.layout = {
            "schema_version": 1, "input_sha256": hashlib.sha256(self.data).hexdigest(),
            "language_id": "ARM:LE:32:Cortex", "compiler_spec": "default", "thumb": True,
            "entry_points": ["0x20004004"],
            "segments": [{"name": "image", "offset": 0, "address": "0x20004000", "size": 128,
                          "read": True, "write": True, "execute": True}],
            "labels": [{"name": "reset_handler", "address": "0x20004004", "function": True,
                        "evidence": "Decoded vector", "confidence": "address-only"}],
        }

    def validate(self, layout=None, data=None):
        return export.normalized_layout(layout or self.layout, self.data if data is None else data)

    def test_cortex_layout_normalizes_without_inventing_type_or_hardware_claim(self):
        result = self.validate()
        self.assertEqual(result["entry_points"], [0x20004004])
        self.assertEqual(result["segments"][0]["address"], 0x20004000)
        self.assertEqual(result["labels"][0]["evidence"], "Decoded vector")
        self.assertNotIn("hardware_model", result)
        self.assertEqual(result, self.validate())

    def test_esp32_segments_keep_distinct_data_code_and_zero_length_records(self):
        layout = {**self.layout, "language_id": "Xtensa:LE:32:default", "thumb": False,
                  "entry_points": [0x400d0008], "labels": [], "segments": [
                      {"name": "drom", "offset": 0, "address": 0x3f400020, "size": 32,
                       "read": True, "write": False, "execute": False},
                      {"name": "dram", "offset": 32, "address": 0x3ffbdb60, "size": 16,
                       "read": True, "write": True, "execute": False},
                      {"name": "irom", "offset": 48, "address": 0x400d0000, "size": 80,
                       "read": True, "write": False, "execute": True},
                      {"name": "empty", "offset": 128, "address": 0x3ff80000, "size": 0,
                       "read": True, "write": True, "execute": False}]}
        result = self.validate(layout)
        self.assertEqual(len(result["segments"]), 4)
        self.assertEqual(result["segments"][-1]["size"], 0)
        self.assertFalse(result["segments"][0]["execute"])
        self.assertTrue(result["segments"][2]["execute"])

    def test_input_sha_is_required_and_verified_before_mapping(self):
        for mutate in (lambda x: x.pop("input_sha256"),
                       lambda x: x.update(input_sha256="0" * 64)):
            layout = copy.deepcopy(self.layout)
            mutate(layout)
            with self.assertRaisesRegex(ValueError, "SHA-256|input_sha256"):
                self.validate(layout)
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.validate(data=self.data[:-1] + b"x")

    def test_wrong_processor_or_context_is_rejected(self):
        for field, value in (("language_id", "x86:LE:64:default"), ("compiler_spec", "gcc"),
                             ("thumb", False), ("thumb", 1)):
            layout = copy.deepcopy(self.layout)
            layout[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.validate(layout)

    def test_segment_bounds_overlap_and_permissions_are_not_guessed(self):
        for field, value in (("size", 129), ("offset", 1), ("address", 0xfffffff0),
                             ("read", False), ("execute", "yes"), ("address", 0x20004001)):
            layout = copy.deepcopy(self.layout)
            layout["segments"][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.validate(layout)
        layout = copy.deepcopy(self.layout)
        layout["segments"].append({**layout["segments"][0], "name": "overlap"})
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            self.validate(layout)

    def test_entry_points_and_function_labels_require_executable_bytes(self):
        for address in (0x20003ffe, 0x20004080, 0x20004005):
            layout = copy.deepcopy(self.layout)
            layout["entry_points"] = [address]
            with self.subTest(address=address), self.assertRaises(ValueError):
                self.validate(layout)
        layout = copy.deepcopy(self.layout)
        layout["segments"][0]["execute"] = False
        with self.assertRaisesRegex(ValueError, "executable"):
            self.validate(layout)
        layout = copy.deepcopy(self.layout)
        layout["labels"][0]["address"] = 0x20005000
        with self.assertRaisesRegex(ValueError, "memory type"):
            self.validate(layout)

    def test_numeric_fields_and_duplicate_labels_fail_closed(self):
        for value in (-1, 1.5, True, "invalid", 0x100000000):
            with self.subTest(value=value), self.assertRaises(ValueError):
                export.integer(value, "address")
        layout = copy.deepcopy(self.layout)
        layout["labels"] *= 2
        with self.assertRaisesRegex(ValueError, "Duplicate label"):
            self.validate(layout)
        layout = copy.deepcopy(self.layout)
        layout["segments"][0]["name"] = "../escape"
        with self.assertRaisesRegex(ValueError, "identifiers"):
            self.validate(layout)

    def test_duplicate_json_fields_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "layout.json"
            path.write_text('{"schema_version":1,"schema_version":2}')
            with self.assertRaisesRegex(ValueError, "Duplicate JSON"):
                export.read_json(path)

    def test_archive_pins_reject_replaced_distribution(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fake.zip"
            path.write_bytes(b"not the pinned tool")
            with self.assertRaisesRegex(ValueError, "pinned distribution"):
                export.tool_evidence(Path(temporary), Path(temporary), {"ghidra": path}, "ARM:LE:32:Cortex")


if __name__ == "__main__":
    unittest.main()
