"""USB program and undo a real provisioned image using a memory-backed programmer."""

import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build
import usb_application
import usb_avr as avr
import usb_stock as stock


class MemoryProgrammer:
    def __init__(self, flash):
        self.flash = flash
        self.identity = ("virtual", "synthetic-device", "usb:1")
        self.writes = 0
        self.interrupt = False

    def run(self, work, operations, *, read_only):
        if read_only:
            signature, flash = operations
            assert signature.startswith("signature:r:") and flash.startswith("flash:r:")
            (work / signature.split(":")[2]).write_bytes(avr.SIGNATURE)
            (work / flash.split(":")[2]).write_bytes(self.flash)
            return
        assert operations == ["flash:v:read-1.bin:r", "flash:w:app-pages.bin:r"]
        assert self.flash == (work / "read-1.bin").read_bytes()
        pages = (work / "app-pages.bin").read_bytes()
        assert len(pages) <= avr.BOOT_MIN and len(pages) % avr.PAGE == 0
        self.writes += 1
        if self.interrupt:
            start = usb_application.end_page(len(pages)) - 3 * avr.PAGE
            self.flash = pages[:start] + self.flash[start:]
            raise ValueError("Interrupted programming")
        self.flash = pages + self.flash[len(pages):]


@unittest.skipUnless(os.environ.get("ZYMATIC_TEST_HEX"), "point ZYMATIC_TEST_HEX at the verified factory HEX or ZIP")
class ApplicationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = stock.decode_application(stock.unpack_source(Path(os.environ['ZYMATIC_TEST_HEX']).read_bytes()))

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.data = Path(temp.name) / "hub"
        self.key = self.data / "device.key"
        # Keep program expansion erased and put nontrivial bytes into the
        # protected boot section so accidental full-chip replacement is visible.
        self.before = self.original.ljust(avr.BOOT_MIN, b'\xff') + bytes(range(256)) * ((avr.FLASH - avr.BOOT_MIN) // 256)
        self.device = MemoryProgrammer(self.before)
        controller = patch.object(avr, "Controller", return_value=self.device)
        controller.start()
        self.addCleanup(controller.stop)

    def perform(self, action, answers, **options):
        confirmations = iter(answers)
        avr.service(self.data, "virtual", action, confirm=lambda _: next(confirmations), **options)

    def install(self, **options):
        self.perform("install", ["INSTALL ZYMATIC", "WRITE " + build.VERSION], key_file=self.key, **options)

    def restore(self):
        self.perform("restore", ["RESTORE ZYMATIC", "WRITE 1.1.14"])

    def test_install_reuse_identity_and_restore_including_credential_pages(self):
        program = {"name": "My recipe", "steps": [
            {"name": "Circulate", "target_f": 0, "minutes": 3, "drain_minutes": 0, "location": 0}]}
        self.install(hub_host="brew.lan", offline_program=program)
        identity = build.recognize_private_application(self.device.flash)
        self.assertEqual(identity["hub_host"], "brew.lan")
        self.assertEqual(len(identity["uid"]), 32)
        self.assertEqual(self.device.flash[avr.BOOT_MIN:], self.before[avr.BOOT_MIN:])
        self.install()
        self.assertEqual(self.device.writes, 1)
        self.install(hub_host="other.lan")
        self.assertEqual(build.recognize_private_application(self.device.flash)["uid"], identity["uid"])
        self.assertEqual(build.recognize_private_application(self.device.flash)["offline_program"]["name"], "My recipe")
        self.restore()
        self.assertEqual(self.device.flash, self.before)

    def test_rejects_occupied_or_unknown_stock_and_unconfirmed_install(self):
        with self.assertRaisesRegex(ValueError, "canceled"):
            self.perform("install", ["no"], key_file=self.key)
        mixed = bytearray(self.before)
        mixed[avr.BOOT_MIN - 1] = 0x22
        self.device.flash = bytes(mixed)
        with self.assertRaisesRegex(ValueError, "Occupied flash"):
            self.install()
        self.device.flash = b'\x33' + self.before[1:]
        with self.assertRaisesRegex(ValueError, "requires verified"):
            self.install()
        self.assertEqual(self.device.writes, 0)

    def test_incomplete_install_restores_from_verified_private_transaction(self):
        self.device.interrupt = True
        with self.assertRaisesRegex(ValueError, "Interrupted"):
            self.install()
        self.assertIsNone(build.recognize_private_application(self.device.flash))
        self.device.interrupt = False
        self.restore()
        self.assertEqual(self.device.flash, self.before)


if __name__ == '__main__':
    unittest.main()
