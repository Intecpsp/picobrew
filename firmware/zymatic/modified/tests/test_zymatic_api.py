"""Exercise the Zymatic HTTP service and both transport models."""
from pathlib import Path
import os
import sys
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NetworkTests(unittest.TestCase):
    def test_device_website_controls_the_displayed_revision(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for the browser script test")
        subprocess.run([node, str(ROOT / "tests/panel-site.cjs")], check=True, timeout=10)

    def test_register_io_http_authentication_and_ring_wrap(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            self.skipTest("Native C compiler is required")
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp, "zymatic-network-test.exe")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                            str(ROOT / "zymatic_api.c"), str(ROOT / "zymatic_api_w5100.c"),
                            str(ROOT / "src/history.c"), str(ROOT / "src/pairing.c"), str(ROOT / "tests/history_native.c"),
                            str(ROOT / "tests/zymatic_api_native.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_wifi_http_session_with_real_api_and_site(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            self.skipTest("Native C compiler is required")
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp, "zymatic-wifi-http-test.exe")
            sources = ["zymatic_api.c", "src/history.c", "src/pairing.c", "tests/history_native.c",
                       "wifi/http_session.c", "wifi/server.c", "wifi/cc3000.c", "wifi/cc3000_broker.c", "wifi/tests/http_session_native.c"]
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                            *(str(ROOT / path) for path in sources), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_radio_commands_and_completions(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            self.skipTest("Native C compiler is required")
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp, "cc3000-socket-test.exe")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(ROOT / "wifi/cc3000.c"),
                            str(ROOT / "wifi/tests/cc3000_native.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)
            original = os.environ.get("ZYMATIC_TEST_HEX")
            if original:
                sys.path.insert(0, str(ROOT / "tools"))
                import usb_stock
                binary = Path(temp, "original.bin")
                binary.write_bytes(usb_stock.decode_application(usb_stock.unpack_source(Path(original).read_bytes())))
                command = ["node", str(ROOT / "wifi/tests/cc3000_oem.cjs"), binary, exe]
                if toolchain := os.environ.get("ZYMATIC_TEST_AVR_BIN"):
                    command.append(toolchain)
                subprocess.run(command, check=True, timeout=30)
            broker = Path(temp, "cc3000-broker-test.exe")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(ROOT / "wifi/cc3000.c"),
                            str(ROOT / "wifi/cc3000_broker.c"), str(ROOT / "wifi/tests/cc3000_broker_native.c"),
                            "-o", str(broker)], check=True)
            subprocess.run([str(broker)], check=True, timeout=10)

if __name__ == "__main__":
    unittest.main()
