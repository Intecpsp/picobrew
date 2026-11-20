"""Check container boundaries against every preserved accessory image."""
import hashlib
from pathlib import Path
import struct
import unittest

from esp_image import ImageError, mapped_offset, parse_esp32, parse_esp8266_arduino

ROOT = Path(__file__).resolve().parents[2]


class EspImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.still = (ROOT / "firmware/picostill/oem/upstream/picostill_0_0_35.bin").read_bytes()
        cls.ferm = (ROOT / "firmware/picoferm/oem/upstream/picoferm_0_2_7.bin").read_bytes()

    def test_every_preserved_accessory_image_and_segment_identity(self):
        for model, reader, expected in (("picostill", parse_esp32, 6),
                                         ("picoferm", parse_esp8266_arduino, 16)):
            paths = sorted((ROOT / "firmware" / model / "oem/upstream").glob("*.bin"))
            self.assertEqual(len(paths), expected)
            for path in paths:
                with self.subTest(path=path.name):
                    raw = path.read_bytes()
                    image = reader(raw)
                    self.assertEqual(image["sha256"], hashlib.sha256(raw).hexdigest())
                    self.assertTrue(image["checksum_verified"])
                    self.assertFalse(image["publisher_signature_verified"])
                    components = [image] if model == "picostill" else [image["bootloader"], image["application"]]
                    for component in components:
                        for segment in component["segments"]:
                            offset, size = segment["file_offset"], segment["size_bytes"]
                            self.assertEqual(segment["sha256"], hashlib.sha256(raw[offset:offset+size]).hexdigest())
                            self.assertEqual(mapped_offset(image, segment["load_address"], size), offset)

    def test_latest_maps_have_independently_observed_entries_and_extents(self):
        still = parse_esp32(self.still)
        self.assertEqual((still["entry_address"], still["segment_count"]), (0x400836f8, 7))
        self.assertEqual(still["segments"][0]["load_address"], 0x3f400020)
        self.assertTrue(still["digest_verified"])
        ferm = parse_esp8266_arduino(self.ferm)
        self.assertEqual(ferm["bootloader"]["entry_address"], 0x4010f29c)
        self.assertEqual(ferm["application"]["entry_address"], 0x40100004)
        self.assertEqual(ferm["application"]["segments"][0]["file_offset"], 4112)
        self.assertEqual(ferm["application"]["segments"][0]["load_address"], 0x40201010)
        self.assertFalse(ferm["digest_verified"])

    def test_payload_corruption_rejected_by_component_checksum(self):
        for raw, reader, offsets in ((self.still, parse_esp32, (32,)),
                                     (self.ferm, parse_esp8266_arduino, (16, 4112))):
            for offset in offsets:
                with self.subTest(offset=offset):
                    changed = bytearray(raw)
                    changed[offset] ^= 1
                    with self.assertRaisesRegex(ImageError, "checksum"):
                        reader(bytes(changed))

    def test_appended_digest_detects_a_checksum_preserving_edit(self):
        raw = bytearray(self.still)
        raw[32] ^= 1
        raw[33] ^= 1
        with self.assertRaisesRegex(ImageError, "SHA-256"):
            parse_esp32(bytes(raw))

    def test_truncation_and_trailing_data_are_not_silently_accepted(self):
        for raw, reader in ((self.still, parse_esp32), (self.ferm, parse_esp8266_arduino)):
            for changed in (b"", raw[:7], raw[:23], raw[:4096], raw[:-1], raw+b"\0"):
                with self.subTest(length=len(changed), reader=reader.__name__):
                    with self.assertRaises(ImageError):
                        reader(changed)

    def test_unsupported_chip_and_invalid_magic_count_mode_rejected(self):
        for offset, value in ((0, 0), (1, 0), (1, 17), (2, 4), (12, 1), (23, 2)):
            changed = bytearray(self.still)
            changed[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ImageError):
                parse_esp32(bytes(changed))

    def test_out_of_bounds_and_overlapping_segments_rejected(self):
        for raw, reader, header in ((self.still, parse_esp32, 24),
                                    (self.ferm, parse_esp8266_arduino, 0x1008)):
            changed = bytearray(raw)
            struct.pack_into("<I", changed, header+4, 0xffffffff)
            with self.assertRaisesRegex(ImageError, "truncated or overflowing"):
                reader(bytes(changed))
            image = reader(raw)
            segments = image.get("segments", image.get("application", {}).get("segments"))
            changed = bytearray(raw)
            struct.pack_into("<I", changed, segments[1]["header_offset"], segments[0]["load_address"])
            with self.assertRaisesRegex(ImageError, "overlapping"):
                reader(bytes(changed))

    def test_arduino_sector_padding_is_verified(self):
        changed = bytearray(self.ferm)
        changed[2048] = 0
        with self.assertRaisesRegex(ImageError, "sector padding"):
            parse_esp8266_arduino(bytes(changed))

    def test_mapping_never_spans_regions_or_accepts_negative_sizes(self):
        image = parse_esp32(self.still)
        segment = image["segments"][0]
        for address, size in ((0, 1), (-1, 1), (0xffffffff, 2),
                              (segment["load_address"], -1),
                              (segment["load_address"]+segment["size_bytes"]-1, 2)):
            with self.subTest(address=address, size=size), self.assertRaises(ImageError):
                mapped_offset(image, address, size)


if __name__ == "__main__":
    unittest.main()
