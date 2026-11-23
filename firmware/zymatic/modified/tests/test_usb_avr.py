import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import usb_avr as avr
import usb_stock as stock


class SimulatedController:
    def __init__(self, image):
        self.image = image
        self.calls = []
        self.mutate_read = {}

    def run(self, work, operations, *, read_only):
        self.calls.append((operations, read_only))
        if read_only:
            signature, flash = operations
            self.assert_operation(signature, "signature:r:")
            self.assert_operation(flash, "flash:r:")
            (work / signature.split(":")[2]).write_bytes(avr.SIGNATURE)
            data = self.mutate_read.get(len(self.calls), self.image)
            (work / flash.split(":")[2]).write_bytes(data)
            return
        assert operations == ["flash:v:read-1.bin:r", "flash:w:app-pages.bin:r"]
        assert (work / "read-1.bin").read_bytes() == self.image
        pages = (work / "app-pages.bin").read_bytes()
        assert len(pages) == 444 * 256
        self.image = pages + self.image[len(pages):]

    def assert_operation(self, value, prefix):
        assert value.startswith(prefix) and value.endswith(":r")


class USBTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.data = Path(temporary.name)
        self.cache = self.data / "usb-stock" / "zymatic"
        self.before = bytes((i + i // 97) % 256 for i in range(avr.FLASH))
        self.app = bytes((i // 37 + i) % 256 for i in range(stock.APP_SIZE))
        self.avr = SimulatedController(self.before)
        for p in [patch.object(stock, "APP_SHA256", hashlib.sha256(self.app).hexdigest()),
                  patch.object(avr, "Controller", return_value=self.avr)]:
            p.start()
            self.addCleanup(p.stop)

    def restore(self, answers=("RESTORE ZYMATIC", "WRITE 1.1.14")):
        it = iter(answers)
        avr.service(self.data, "test", "restore", confirm=lambda _: next(it))

    def stash(self):
        stock.private_files.ensure_private_directory(self.cache, parents=True, exist_ok=True)
        stock.save_private(self.cache / avr.OEM_PREFIX, self.app)

    def test_read_only_uncertified_backup_and_source_validation(self):
        avr.service(self.data, "test", "backup")
        paths = list(self.cache.glob("avr-flash-*.bin"))
        self.assertEqual(len(paths), 1)
        self.assertEqual(stock.private_files.read_regular(paths[0], avr.FLASH, private=True), self.before)
        report = self.cache / f"boot-evidence-v1-{hashlib.sha256(self.before).hexdigest()}.json"
        self.assertEqual(stock.private_files.read_regular(report, 4096, private=True),
                         avr.boot_evidence.as_json(self.before))
        self.assertFalse((self.cache / avr.OEM_PREFIX).exists())
        self.assertEqual([c[1] for c in self.avr.calls], [True, True])
        stock.save_private(self.cache / avr.OEM_PREFIX, self.before[:stock.APP_SIZE])
        with self.assertRaisesRegex(ValueError, "verified stock"):
            self.restore()
        self.assertTrue(all(call[1] for call in self.avr.calls))

    def test_original_saved_once_reused_with_exact_program_tail_preserved(self):
        self.avr.image = self.app + self.before[len(self.app):]
        avr.service(self.data, "test", "backup")
        self.assertEqual((self.cache / avr.OEM_PREFIX).read_bytes(), self.app)
        self.restore(())  # same-version native restoration needs zero writes
        self.assertTrue(all(c[1] for c in self.avr.calls))
        self.avr.image = self.before
        with patch.object(stock, "fetch_source", side_effect=AssertionError("unexpected network")):
            self.restore()
        self.assertEqual(self.avr.image, self.app + self.before[len(self.app):])
        self.assertEqual([c[1] for c in self.avr.calls[-4:]], [True, True, False, True])

    def test_confirmation_and_clipped_protected_or_inconsistent_reads(self):
        self.stash()
        for answers in [("no",), ("RESTORE ZYMATIC", "no")]:
            with self.assertRaisesRegex(ValueError, "canceled"):
                self.restore(answers)
        for image in [self.before[:-1], bytes(avr.FLASH), bytes([0xff])*avr.FLASH,
                      bytes(range(256)) * (avr.FLASH // 256)]:
            self.avr.image = image
            with self.assertRaisesRegex(ValueError, "clipped"):
                self.restore()
        self.avr.image = self.before
        self.avr.calls = []
        self.avr.mutate_read[2] = self.before[:-1] + bytes([self.before[-1] ^ 1])
        with self.assertRaisesRegex(ValueError, "reads differ"):
            self.restore()
        self.assertTrue(all(call[1] for call in self.avr.calls))

    def test_independent_readback_catches_adjacent_flash_corruption(self):
        self.stash()
        self.avr.mutate_read[4] = self.app + self.before[len(self.app):-1] + bytes([self.before[-1] ^ 1])
        with self.assertRaisesRegex(ValueError, "Final flash readback"):
            self.restore()


class ProgrammerTests(unittest.TestCase):
    def test_requires_exact_usb_port_and_vendor_id(self):
        right = SimpleNamespace(device="COM4", vid=0x20a0, pid=0x421e,
                                serial_number="123", location="1-1")
        other = SimpleNamespace(**vars(right))
        other.pid = 0x1234
        self.assertEqual(avr.select_usb_port("COM4", [right]), ("COM4", "123", "1-1"))
        for name, devices in [("COM7", [right]), ("COM4", [right, right]), ("COM4", [other])]:
            with self.assertRaisesRegex(ValueError, "PicoBrew VID"):
                avr.select_usb_port(name, devices)

    def test_command_rejects_unidentifiable_target_for_writes(self):
        with patch.object(avr, "usb_identity", return_value=("COM4", None, None)), patch.object(
                avr, "programmer", return_value="avrdude"), patch.object(avr.subprocess, "run") as run:
            controller = avr.Controller("COM4")
            with self.assertRaisesRegex(ValueError, "location or serial number"):
                controller.run(".", ["flash:w:app-pages.bin:r"], read_only=False)
            run.assert_not_called()

    def test_wrong_controller_signature_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            class OtherController:
                def run(self, path, operations, *, read_only):
                    (path / "signature-1.bin").write_bytes(bytes.fromhex("1e9706"))
            with self.assertRaisesRegex(ValueError, "signature"):
                avr.read_flash(OtherController(), work, 1)

    def test_read_and_write_commands_keep_signature_verification_and_disable_erase(self):
        identity = ("COM4", "one", "usb1")
        with patch.object(avr, "usb_identity", side_effect=[identity, identity, identity, ("COM4", "two", "usb2")]), patch.object(
                avr, "programmer", return_value="avrdude"), patch.object(avr.subprocess, "run") as run:
            run.return_value.returncode = 0
            controller = avr.Controller("COM4")
            controller.run(".", ["flash:r:read.bin:r"], read_only=True)
            argv = run.call_args.args[0]
            self.assertEqual(argv, ["avrdude", "-N", "-c", "arduino", "-p", "m1284p",
                                    "-P", "COM4", "-b", "115200", "-D", "-A", "-n",
                                    "-U", "flash:r:read.bin:r"])
            ops = ["flash:v:read-1.bin:r", "flash:w:app-pages.bin:r"]
            controller.run(".", ops, read_only=False)
            self.assertEqual(run.call_args.args[0], argv[:-3] + ["-U", ops[0], "-U", ops[1]])
            with self.assertRaisesRegex(ValueError, "identity changed"):
                controller.run(".", ops, read_only=False)
            self.assertEqual(run.call_count, 2)


if __name__ == "__main__":
    unittest.main()
