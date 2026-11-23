from hashlib import sha256
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import boot_evidence as boot


class BootEvidenceTests(unittest.TestCase):
    def test_regions_use_byte_addresses_and_only_exact_references_match(self):
        data = bytearray(boot.FLASH_BYTES)
        data[-1024:] = bytes(range(256)) * 4
        digest = sha256(data[-1024:]).hexdigest()
        with patch.dict(boot.REFERENCE_IMAGES, {(1024, digest): "test fixture"}):
            report = boot.describe(data)
            windows = report["possible_boot_regions"]
            self.assertEqual([s["start_byte"] for s in windows], [0x1e000, 0x1f000, 0x1f800, 0x1fc00])
            self.assertEqual([s["matching_reference"] for s in windows], [None, None, None, "test fixture"])
            self.assertEqual(windows[-1]["read_pattern"], "repeated-page")
            data[-1] ^= 1
            changed = boot.describe(data)["possible_boot_regions"][-1]
            self.assertIsNone(changed["matching_reference"])
            self.assertEqual(changed["read_pattern"], "mixed")

    def test_uniform_reads_are_observations_and_have_no_matching_bootloader(self):
        for byte, pattern in [(0xff, "all-ff"), (0, "all-00")]:
            for window in boot.describe(bytes([byte]) * boot.FLASH_BYTES)["possible_boot_regions"]:
                self.assertEqual(window["read_pattern"], pattern)
                self.assertIsNone(window["matching_reference"])
        for size in (113610, 122624, boot.FLASH_BYTES - 1, boot.FLASH_BYTES + 1):
            with self.assertRaises(ValueError):
                boot.describe(bytes(size))


if __name__ == "__main__":
    unittest.main()
