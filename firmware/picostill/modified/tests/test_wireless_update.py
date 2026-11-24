from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT.parents[1] / 'common'


class WirelessUpdateTests(unittest.TestCase):
    def test_authenticated_transfer_and_power_loss(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'wireless-update'
            subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'include'),
                            '-I'+str(COMMON/'include'), str(ROOT/'src/wireless_update.cpp'),
                            str(ROOT/'tests/wireless_update_native.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)

    def test_factory_flash_faults(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'factory-flash'
            subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'include'),
                            '-I'+str(COMMON/'include'), '-I'+str(ROOT/'platform/esp32'), '-I'+str(ROOT/'tests/ota_sdk'),
                            str(ROOT/'platform/esp32/factory_flash.cpp'), str(ROOT/'tests/factory_flash_native.cpp'),
                            '-o', exe], check=True)
            subprocess.run([exe], check=True)
