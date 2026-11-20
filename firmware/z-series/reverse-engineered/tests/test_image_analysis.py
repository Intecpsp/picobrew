"""Offline format checks against actual preserved images and malformed fixtures."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("z_image_analysis", ROOT / "tools" / "analyze_images.py")
image = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(image)


def fixture(segments=None, digest=True, entry=0x40080000):
    if segments is None:
        segments = [(0x40080000, b"\x36\x41\x00\x00"), (0x3F400020, b"example\0")]
    header = bytearray(24)
    header[:4] = bytes((0xE9, len(segments), 2, 0x4F))
    struct.pack_into("<I", header, 4, entry)
    header[8] = 0xEE
    header[23] = int(digest)
    data = bytearray(header)
    checksum = 0xEF
    for address, payload in segments:
        data += struct.pack("<II", address, len(payload)) + payload
        for byte in payload:
            checksum ^= byte
    while len(data) % 16 != 15:
        data.append(0)
    data.append(checksum)
    if digest:
        data += hashlib.sha256(data).digest()
    return bytes(data)


class ImageTests(unittest.TestCase):
    def test_preserved_oem_images_have_valid_segment_checksums_and_digests(self):
        reports, sources = image.build_reports()
        self.assertEqual(len(reports["images"]), 2)
        for report, inventory in zip(reports["images"], sources["images"]):
            self.assertEqual(report["entry_address"], 0x40083918)
            self.assertEqual(len(report["segments"]), 12)
            self.assertEqual(report["sha256"], image.IMAGES[report["filename"]])
            self.assertTrue(report["checksum_verified"])
            self.assertTrue(report["digest_verified"])
            self.assertFalse(report["publisher_signature_verified"])
            self.assertFalse(report["flash_configuration_is_hardware_identification"])
            headers = {record["file"] for record in inventory["application_headers"]}
            self.assertTrue({"Msp.h", "Safeties.h", "Runtime.h", "Heater.h", "MAX11613.h"} <= headers)
            self.assertFalse(inventory["function_behaviors_reconstructed_by_this_inventory"])

    def test_empty_segments_are_recorded_without_claiming_mapped_bytes(self):
        data = fixture([(0x40080000, b"1234"), (0x3FF80000, b""), (0x3FF80000, b"")])
        self.assertEqual([s["size_bytes"] for s in image.parse_image(data)["segments"]], [4, 0, 0])

    def test_segment_mutation_fails_even_before_appended_digest(self):
        data = bytearray(fixture())
        data[32] ^= 1
        with self.assertRaisesRegex(image.ImageError, "XOR checksum"):
            image.parse_image(bytes(data))

    def test_header_or_digest_mutation_is_detected(self):
        for offset in (8, -1):
            data = bytearray(fixture())
            data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(image.ImageError, "SHA-256"):
                image.parse_image(bytes(data))

    def test_truncation_or_extra_payload_is_rejected(self):
        data = fixture()
        for bad in (data[:23], data[:30], data[:-1], data + b"\0", data + b"extra"):
            with self.subTest(size=len(bad)), self.assertRaises(image.ImageError):
                image.parse_image(bad)

    def test_unsupported_header_and_segment_bounds_are_rejected(self):
        for offset, value in ((0, 0), (1, 0), (1, 17), (2, 4), (12, 2), (23, 2)):
            data = bytearray(fixture())
            data[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(image.ImageError):
                image.parse_image(bytes(data))
        data = bytearray(fixture())
        struct.pack_into("<I", data, 28, 0xFFFFFFFF)
        with self.assertRaisesRegex(image.ImageError, "overflowing segment"):
            image.parse_image(bytes(data))

    def test_overlap_and_unmapped_entry_are_rejected(self):
        with self.assertRaisesRegex(image.ImageError, "overlapping"):
            image.parse_image(fixture([(0x40080000, b"1234"), (0x40080002, b"5678")]))
        with self.assertRaisesRegex(image.ImageError, "entry point"):
            image.parse_image(fixture(entry=0x3F400020))

    def test_footer_padding_is_checked_and_unsigned_image_is_labeled(self):
        data = bytearray(fixture())
        data[-34] = 1
        data[-32:] = hashlib.sha256(data[:-32]).digest()
        with self.assertRaisesRegex(image.ImageError, "padding"):
            image.parse_image(bytes(data))
        report = image.parse_image(fixture(digest=False))
        self.assertIsNone(report["appended_sha256"])
        self.assertFalse(report["digest_verified"])
        self.assertFalse(report["publisher_signature_verified"])


if __name__ == "__main__":
    unittest.main()
