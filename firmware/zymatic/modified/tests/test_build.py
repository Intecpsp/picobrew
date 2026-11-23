import json
import os
from pathlib import Path
import random
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build
import usb_stock as stock

TEST_KEY = bytes(range(32))
TEST_UID = "a378b5dd028349ad8cdf5dfe52859a36"


class BuildTests(unittest.TestCase):
    def test_shipped_template_matches_source_and_has_no_private_data(self):
        code, record = build.template()
        self.assertLessEqual(build.START + len(code), build.LIMIT)
        for field, size in (build.FIELDS | build.DERIVED).items():
            off = record["symbols"][field] - build.START
            self.assertEqual(code[off:off+size], bytes(size))
        self.assertEqual(record["symbols"]["provision_crc"] + 4, build.START + len(code))
        self.assertIsNone(build.recognize_private_application(bytes(100)))

    def test_bad_credentials_and_destinations(self):
        for value in [bytes(32), b"\xff" * 32, b"short", "x"*32]:
            with self.assertRaisesRegex(ValueError, "API key"):
                build.provisioning(value, TEST_UID, "host")
        for value in ["A"*32, "0"*32, "a", "blank uid"]:
            with self.assertRaisesRegex(ValueError, "UID"):
                build.provisioning(TEST_KEY, value, "host")
        for value in ["", "host\r", "http://hub", "host:0", "host:65536", "host:8:1", ":80",
                      "a..b", "a-.lan", "-a.lan", "x"*64]:
            with self.assertRaises(ValueError):
                build.hub_origin(value)
        self.assertEqual(build.hub_origin("10.0.0.123:8080"), ("10.0.0.123", 8080))
        self.assertEqual(build.hub_origin("hub.local"), ("hub.local", 80))

    def test_hex_high_address_roundtrip(self):
        raw = random.Random(12).randbytes(120000)
        result = bytearray()
        base = None
        lines = build.intel_hex(raw).splitlines()
        self.assertEqual(lines[-1], b":00000001FF")
        for line in lines[:-1]:
            row = bytes.fromhex(line[1:].decode())
            self.assertEqual(len(row), row[0] + 5)
            self.assertEqual(sum(row) % 256, 0)
            address = int.from_bytes(row[1:3], "big")
            if row[3] == 4:
                base = int.from_bytes(row[4:-1], "big") << 16
            else:
                self.assertEqual((base or 0) + address, len(result))
                result.extend(row[4:-1])
        self.assertEqual(result, raw)


@unittest.skipUnless(os.getenv("ZYMATIC_TEST_HEX"), "requires the original public ZIP or HEX")
class ComposedImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = stock.decode_application(stock.unpack_source(Path(os.environ["ZYMATIC_TEST_HEX"]).read_bytes()))

    def test_recognizer_checks_all_regions_and_credential_is_private(self):
        image, manifest = build.build_application(self.original, TEST_KEY, TEST_UID, "my-brewery.lan:8080")
        self.assertEqual(build.recognize_private_application(image.ljust(0x20000, b"\xa5")), manifest)
        self.assertNotIn(TEST_KEY.hex(), json.dumps(manifest))
        self.assertNotIn(TEST_KEY.hex().upper(), json.dumps(manifest))
        self.assertEqual(build.legacy_sum(self.original), 0x4d16)
        self.assertEqual(zlib.crc32(image[:-4]), int.from_bytes(image[-4:], "little"))
        self.assertEqual(len(image), manifest["application_bytes"])
        self.assertIsNone(build.recognize_private_application(self.original))
        offsets = [234, 0x1a7c4, 0x1bbca, build.START, 0x2eaa, 0x42ae, 0xd23a]
        tables = build.tables
        offsets += [tables.POOL, tables.PACKED_END - 1, tables.PACKED_END,
                    tables.POOL_END - 1, tables.CRC_TABLE + 1, tables.CRC_TABLE + 255,
                    tables.CRT, build.START - 1]
        offsets += [field["address"] for field in manifest["config"].values()] + [len(image)-1]
        for offset in offsets:
            damaged = bytearray(image)
            damaged[offset] ^= 1
            self.assertIsNone(build.recognize_private_application(damaged), hex(offset))
        with self.assertRaisesRegex(ValueError, "verified"):
            build.build_application(self.original[:-1], TEST_KEY, TEST_UID)
        selected = {"model": "zymatic", "name": "Test mash", "steps": [
            {"name": "Mash", "target_f": 151, "minutes": 60, "location": 1, "drain_minutes": 5},
        ]}
        custom, result = build.build_application(self.original, TEST_KEY, TEST_UID, "brew.local", selected)
        self.assertEqual(result["offline_program"]["name"], selected["name"])
        self.assertEqual(result["offline_program"]["steps"], selected["steps"])
        self.assertEqual(build.recognize_private_application(custom), result)
        built_again = build.build_application(self.original, TEST_KEY, TEST_UID, "my-brewery.lan:8080")
        self.assertEqual(built_again, (image, manifest))


if __name__ == "__main__":
    unittest.main()
