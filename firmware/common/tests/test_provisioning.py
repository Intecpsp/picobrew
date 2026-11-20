import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProvisioningTests(unittest.TestCase):
    def test_physical_console_verified_persistence(self):
        with tempfile.TemporaryDirectory(prefix="provisioning-") as temporary:
            executable = Path(temporary) / "provisioning"
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                            "-I" + str(ROOT / "include"), str(ROOT / "src/provisioning.cpp"),
                            str(ROOT / "src/records.cpp"), str(ROOT / "tests/provisioning_test.cpp"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
