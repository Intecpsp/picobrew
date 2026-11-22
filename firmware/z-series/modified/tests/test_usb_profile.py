import binascii
import hashlib
from pathlib import Path
import struct
import sys
import unittest

MODIFIED = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MODIFIED / "tools"))
import usb_profile
from esp_stock_partitions import partitions, qualify_install_layout, selected_app_slice

FACTORY = (0, 0, 0x10000, 0x1f0000, b"factory")
NVS = (1, 2, 0x9000, 0x5000, b"nvs")
OTA = (1, 0, 0xe000, 0x2000, b"otadata")
FS = (1, 0x82, 0x200000, 0x200000, b"spiffs")


def put_table(flash, rows, md5=True):
    table = b"".join(struct.pack("<HBBII16sI", 0x50aa, *r, 0) for r in rows)
    if md5:
        table += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(table).digest()
    flash[0x8000:0x9000] = table.ljust(0x1000, b"\xff")


def make_flash(rows=(NVS, OTA, FACTORY, FS), version="119"):
    flash = bytearray(b"\xff" * 0x400000)
    address = 0x40078000
    boot = struct.pack("<BBBBI16xII", 0xe9, 1, 2, 0x20, address, address, 4) + bytes(4)
    boot = boot.ljust(47, b"\x00") + b"\xef"
    flash[0x1000:0x1000 + len(boot)] = boot
    put_table(flash, rows)
    app = (MODIFIED.parent / "oem" / "upstream" / f"zseries_0_0_{version}.bin").read_bytes()
    flash[0x10000:0x10000 + len(app)] = app
    return flash


def put_selection(flash, offset, seq, state=0xffffffff):
    record = struct.pack("<I", seq)
    flash[offset:offset + 32] = record + b"\xff" * 20 + struct.pack(
        "<II", state, binascii.crc32(record, 0xffffffff))


class ZStockUSBTests(unittest.TestCase):
    def test_archived_sources_and_model_rules(self):
        for version in ("116", "119"):
            flash = make_flash(version=version)
            for model in ("z-series", "z1", "z2", "z3", "z4"):
                self.assertEqual(usb_profile.qualify_stock(flash, model), "0.0." + version)
            with self.assertRaisesRegex(ValueError, "module"):
                usb_profile.qualify_install_layout(flash, "z2")
            if version == "119":
                usb_profile.qualify_install_layout(flash, "z-series")
            else:
                with self.assertRaisesRegex(ValueError, "0.0.119"):
                    usb_profile.qualify_install_layout(flash, "z-series")

    def test_nonbootable_or_altered_input(self):
        flash = make_flash()
        for pos in (0x1000, 0x102f, 0x10100):
            with self.subTest(offset=pos):
                flash[pos] ^= 1
                with self.assertRaises(ValueError):
                    usb_profile.qualify_stock(flash, "z-series")
                flash[pos] ^= 1

    def test_legacy_table_and_corrupt_table(self):
        flash = make_flash()
        put_table(flash, (NVS, FACTORY, FS), md5=False)
        self.assertEqual(selected_app_slice(flash), slice(0x10000, 0x200000))
        put_table(flash, (NVS, FACTORY, FS))
        flash[0x8000 + 3 * 32 + 20] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            partitions(flash)
        put_table(flash, (NVS, OTA, FACTORY, (0, 0x10, 0x10000, 0x10000, b"ota_0")))
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            partitions(flash)

    def test_only_hash_boot_selected_slot(self):
        rows = (NVS, OTA, (0, 0x10, 0x10000, 0x1f0000, b"ota_0"),
                (0, 0x11, 0x200000, 0x200000, b"ota_1"))
        flash = make_flash(rows)
        self.assertEqual(usb_profile.qualify_stock(flash, "z-series"), "0.0.119")
        # ESP-IDF's CRC for selection 1 is 0x4743989a.
        put_selection(flash, 0xe000, 1)
        self.assertEqual(flash[0xe01c:0xe020], bytes.fromhex("9a984347"))
        put_selection(flash, 0xf000, 2)
        self.assertEqual(selected_app_slice(flash), slice(0x200000, 0x400000))
        with self.assertRaisesRegex(ValueError, "does not match"):
            usb_profile.qualify_stock(flash, "z-series")
        flash[0xf01c] ^= 1
        with self.assertRaisesRegex(ValueError, "boot selection"):
            selected_app_slice(flash)
        put_selection(flash, 0xf000, 3, state=1)
        with self.assertRaisesRegex(ValueError, "boot selection"):
            selected_app_slice(flash)

    def test_private_regions_survive_conversion(self):
        flash = make_flash()
        flash[0x9100] = 41
        qualify_install_layout(flash)
        for rows, unmatched in [
                ((FACTORY, OTA, FS), "NVS"),
                ((NVS, OTA, FACTORY, (1, 1, 0x200000, 0x1000, b"phy")), "displace"),
                ((NVS, FACTORY, (1, 0, 0x200000, 0x2000, b"ota")), "OTA")]:
            put_table(flash, rows)
            with self.assertRaisesRegex(ValueError, unmatched):
                qualify_install_layout(flash)
        put_table(flash, (NVS, OTA, FACTORY))
        for pos in (0x4000, 0x8c00, 0x201000):
            flash[pos] = 41
            with self.assertRaisesRegex(ValueError, "Unidentified"):
                qualify_install_layout(flash)
            flash[pos] = 0xff

    def test_every_occupied_app_slot_must_be_stock(self):
        rows = (NVS, OTA, (0, 0x10, 0x10000, 0x1f0000, b"ota_0"),
                (0, 0x11, 0x200000, 0x200000, b"ota_1"))
        flash = make_flash(rows)
        original = (MODIFIED.parent / "oem/upstream/zseries_0_0_116.bin").read_bytes()
        flash[0x200000:0x200000 + len(original)] = original
        self.assertEqual(usb_profile.qualify_stock(flash, "z-series"), "0.0.119")
        put_selection(flash, 0xe000, 2)
        self.assertEqual(usb_profile.qualify_stock(flash, "z-series"), "0.0.116")
        flash[0x10001] ^= 1
        with self.assertRaisesRegex(ValueError, "ota_0"):
            usb_profile.qualify_stock(flash, "z-series")


if __name__ == "__main__":
    unittest.main()
