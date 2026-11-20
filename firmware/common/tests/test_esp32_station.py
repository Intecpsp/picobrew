"""The production SDK station adapter handles full-width fields and failures."""
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class StationTests(unittest.TestCase):
    def test_bounded_ram_configuration_and_errors(self):
        output=ROOT/'build/station-native';output.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'tests/station_shim'),'-I'+str(ROOT/'platform/esp32'),
                        str(ROOT/'platform/esp32/station.cpp'),str(ROOT/'tests/station_native.cpp'),'-o',str(output)],check=True)
        print(subprocess.check_output([str(output)],text=True).strip())
if __name__=='__main__':unittest.main()
