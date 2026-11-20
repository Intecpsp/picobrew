#!/usr/bin/env python3
"""Exercise production storage transactions at every filesystem failure point."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class VerifiedStorageTests(unittest.TestCase):
    def test_faults_recovery_boundaries_and_exact_readback(self):
        with tempfile.TemporaryDirectory(prefix='picobrew-verified-storage-') as temporary:
            binary = Path(temporary) / 'verified-storage'
            subprocess.run(['g++', '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'include'),
                            str(ROOT / 'src/verified_storage.cpp'),
                            str(ROOT / 'tests/verified_storage_test.cpp'), '-o', str(binary)], check=True)
            report = json.loads(subprocess.check_output([str(binary)], text=True))
        self.assertTrue(report['passed'])
        self.assertGreater(report['injected_callback_failures'], 25)
        self.assertGreater(report['interrupted_transaction_states'], 40)
        self.assertGreater(report['checks'], 400)

if __name__ == '__main__':
    unittest.main()
