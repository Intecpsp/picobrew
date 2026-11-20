"""Execute production socket/VFS adapters against explicit host SDK boundaries."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT

class PlatformPortsTests(unittest.TestCase):
    def build_and_run(self, name, wraps=()):
        with tempfile.TemporaryDirectory(prefix='picobrew-esp32-port-') as directory:
            binary = Path(directory) / name
            includes = [ROOT/'tests/sdk_shim', ROOT/'platform/esp32', COMMON/'include']
            command = shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic', '-pthread'
            ] + ['-I'+str(path) for path in includes] + [
                str(ROOT/'tests'/f'{name}_host.cpp'),
                str(ROOT/'platform/esp32'/f'{name}.cpp'), '-o', str(binary)
            ] + ['-Wl,--wrap='+symbol for symbol in wraps]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_nonblocking_loopback_io_and_cancelled_dns_lifetime(self):
        self.build_and_run('network_ports')

    def test_checked_vfs_handles_flush_offsets_and_error_classification(self):
        self.build_and_run('storage_ports', ('open','close','stat','rename','unlink'))

if __name__ == '__main__':
    unittest.main()
