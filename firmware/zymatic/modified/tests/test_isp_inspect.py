from hashlib import sha256
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import isp_inspect as isp


class ISPTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        flash = bytes(range(256)) * (isp.usb_avr.FLASH // 256)
        self.readout = {"signature": bytes.fromhex("1e9705"), "lfuse": b"\xf7", "hfuse": b"\xdc",
                        "efuse": b"\xfd", "lock": b"\xef", "flash": flash, "eeprom": bytes(4096)}
        self.backup = self.directory / "serial.bin"
        isp.stock.save_private(self.backup, flash[:isp.usb_avr.BOOT_MIN].ljust(len(flash), b'\xff'))
        for mocker in [patch.object(isp.stock, "APP_SHA256", sha256(flash[:isp.stock.APP_SIZE]).hexdigest()),
                       patch.object(isp.usb_avr, "programmer", return_value="avrdude")]:
            mocker.start()
            self.addCleanup(mocker.stop)

    def run_inspect(self, second=None):
        with patch.object(isp, "read_once", side_effect=[self.readout, second or self.readout]):
            return isp.inspect(self.directory, "J1234567", self.backup)

    def test_report_uses_real_fuse_selection_and_preserves_eeprom(self):
        for bits, expected in enumerate((8192, 4096, 2048, 1024)):
            self.readout["hfuse"] = bytes([0xd8 | bits << 1])
            report = json.loads(self.run_inspect().read_bytes())
            region = report["configured_boot_region"]
            self.assertEqual(region["size_bytes"], expected)
            self.assertEqual(region["sha256"], sha256(self.readout["flash"][-expected:]).hexdigest())
            self.assertEqual(report["reset_destination"], "boot-section")
            self.assertEqual(report["eeprom_sha256"], sha256(self.readout["eeprom"]).hexdigest())
        self.readout["hfuse"] = b"\xdf"
        self.assertEqual(json.loads(self.run_inspect().read_bytes())["reset_destination"], "application")

    def test_inconsistent_isp_or_application_cannot_generate_report(self):
        with self.assertRaisesRegex(ValueError, "reads differ"):
            self.run_inspect(dict(self.readout, lock=b"\xcf"))
        for lock in (0, 1):
            self.readout["lock"] = bytes([lock])
            with self.assertRaisesRegex(ValueError, "lock mode"):
                self.run_inspect()
        self.readout["lock"] = b"\xff"
        self.readout["flash"] = b'X' + self.readout["flash"][1:]
        with self.assertRaisesRegex(ValueError, "differs from the rear-USB backup"):
            self.run_inspect()
        self.assertFalse(list(self.directory.rglob("isp-evidence-*.json")))

    def test_avrdude_command_can_only_read_from_explicit_external_programmer(self):
        with patch.object(isp.subprocess, "run") as process:
            def simulate(argv, *, cwd, **kwargs):
                self.assertEqual(argv[:13], ["avrdude", "-N", "-p", "m1284p", "-c", "atmelice_isp", "-P",
                                             "usb:J1234567", "-B", "10", "-n", "-D", "-A"])
                for i in range(13, len(argv), 2):
                    self.assertEqual(argv[i], "-U")
                    memory, action, filename, typ = argv[i + 1].split(":")
                    self.assertEqual((action, typ), ("r", "r"))
                    (cwd / filename).write_bytes(self.readout[memory])
            process.side_effect = simulate
            self.assertEqual(isp.read_once("avrdude", "J1234567", self.directory), self.readout)
