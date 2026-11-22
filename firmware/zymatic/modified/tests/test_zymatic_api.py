"""Run the real Zymatic request handler over a bounded W5100 register model."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NetworkTests(unittest.TestCase):
    def test_register_io_http_authentication_and_ring_wrap(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            self.skipTest("Native C compiler is required")
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp, "zymatic-network-test.exe")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                            str(ROOT / "zymatic_api.c"), str(ROOT / "zymatic_api_w5100.c"),
                            str(ROOT / "tests/zymatic_api_native.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

if __name__ == "__main__":
    unittest.main()
