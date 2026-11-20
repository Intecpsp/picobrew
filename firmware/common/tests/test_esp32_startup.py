"""The production startup owner retains SDK initialization without NVS erase."""
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
class StartupTests(unittest.TestCase):
    def test_production_initialization_order_and_failure(self):
        output=ROOT/'build/startup-native';output.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-DF_CPU=240000000L',
                        '-I'+str(ROOT/'tests/startup_shim'),'-I'+str(ROOT/'platform/esp32'),
                        str(ROOT/'platform/esp32/startup.cpp'),str(ROOT/'tests/startup_native.cpp'),'-o',str(output)],check=True)
        print(subprocess.check_output([str(output)],text=True).strip())
        symbols=subprocess.check_output(['nm','-C',str(output)],text=True)
        self.assertNotIn('esp_partition_erase_range',symbols)
        self.assertNotIn('nvs_flash_erase',symbols)

if __name__=='__main__':unittest.main()
