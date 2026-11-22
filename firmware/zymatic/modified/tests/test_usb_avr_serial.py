"""Run the real programmer against an in-memory AVR on a private pseudo-terminal."""

import hashlib
import os
from pathlib import Path
import select
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import usb_avr as avr


@unittest.skipUnless(os.name == "posix" and os.environ.get("AVRDUDE_SERIAL_TEST") == "1",
                     "set AVRDUDE_SERIAL_TEST=1 on POSIX with AVRDUDE 8.1 on PATH")
class SerialProtocolTests(unittest.TestCase):
    def setUp(self):
        import pty
        import tty
        self.master, slave = pty.openpty()
        self.addCleanup(os.close, slave)
        self.addCleanup(os.close, self.master)
        tty.setraw(slave)
        self.port = os.ttyname(slave)
        self.before = bytes((i // 83 + i) % 256 for i in range(avr.FLASH))
        self.flash = bytearray(self.before)
        self.application = bytes((i // 37 + i) % 256 for i in range(avr.stock.APP_SIZE))
        self.writes = []
        self.errors = []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.respond, daemon=True)
        self.thread.start()
        self.addCleanup(self.thread.join, 2)
        self.addCleanup(self.stop.set)
        private = tempfile.TemporaryDirectory()
        self.addCleanup(private.cleanup)
        self.data = Path(private.name)
        cache = self.data / "usb-stock" / "zymatic"
        avr.stock.private_files.ensure_private_directory(cache, parents=True)
        avr.stock.save_private(cache / avr.OEM_PREFIX, self.application)

    def read(self, n):
        data = bytearray()
        while len(data) < n:
            if self.stop.is_set():
                raise EOFError
            if select.select([self.master], [], [], .1)[0]:
                data.extend(os.read(self.master, n-len(data)))
        return bytes(data)

    def respond(self):
        offset = 0
        try:
            while True:
                command = self.read(1)[0]
                response = b""
                if command in (0x30, 0x50, 0x51):  # sync / enter / leave programming
                    pass
                elif command == 0x41:  # get programmer version parameter
                    response = bytes([2 if self.read(1)[0] == 0x81 else 0])
                elif command == 0x42:  # set target configuration
                    self.read(20)
                elif command == 0x45:  # extended configuration length includes count byte
                    self.read(self.read(1)[0] - 1)
                elif command == 0x55:
                    offset = int.from_bytes(self.read(2), "little") * 2
                elif command == 0x75:
                    response = avr.SIGNATURE
                elif command in (0x74, 0x64):  # read / program a page
                    size = int.from_bytes(self.read(2), "big")
                    self.assertEqual(self.read(1), b"F")
                    if command == 0x74:
                        response = self.flash[offset:offset+size]
                    else:
                        self.assertEqual(size, 256)
                        self.assertEqual(offset % 256, 0)
                        self.assertLessEqual(offset + size, 113664)
                        self.flash[offset:offset+size] = self.read(size)
                        self.writes.append(offset)
                else:
                    self.fail(f"Unexpected serial command 0x{command:02x}")
                self.assertEqual(self.read(1), b" ")
                os.write(self.master, b"\x14" + response + b"\x10")
        except EOFError:
            return
        except Exception as exc:
            self.errors.append(exc)
            os.write(self.master, b"\x14\x11")

    def service(self, confirm):
        with patch.object(avr, "usb_identity", return_value=(self.port, None, "pseudo-terminal")), patch.object(
                avr.stock, "APP_SHA256", hashlib.sha256(self.application).hexdigest()):
            avr.service(self.data, self.port, "restore", confirm=confirm)

    def test_only_444_application_pages_are_written(self):
        answers = iter(["RESTORE ZYMATIC", "WRITE 1.1.14"])
        self.service(lambda _: next(answers))
        self.assertEqual(self.errors, [])
        self.assertEqual(self.writes, list(range(0, 113664, 256)))
        self.assertEqual(self.flash, self.application + self.before[len(self.application):])

    def test_changed_controller_bytes_abort_before_first_page_write(self):
        def answer(prompt):
            if "WRITE" in prompt:
                self.flash[300] ^= 1
                return "WRITE 1.1.14"
            return "RESTORE ZYMATIC"
        with self.assertRaisesRegex(ValueError, "AVRDUDE failed"):
            self.service(answer)
        self.assertEqual(self.errors, [])
        self.assertEqual(self.writes, [])
