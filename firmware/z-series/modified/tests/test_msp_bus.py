from pathlib import Path
import os,shlex,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class MspBusTests(unittest.TestCase):
    def test_short_reads_timeouts_latched_fault_and_cutoff_retry(self):
        with tempfile.TemporaryDirectory(prefix='z-msp-deadline-')as directory:
            binary=Path(directory)/'msp'
            command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-Wall','-Wextra','-Werror','-pedantic']
            command+=['-I'+str(p)for p in [ROOT/'tests/sdk_shim',ROOT/'platform/esp32',ROOT.parent/'reverse-engineered/include']]
            command += [str(ROOT/'tests/msp_bus_test.cpp'),str(ROOT/'platform/esp32/msp_bus.cpp'),'-o',str(binary)]
            subprocess.run(command,check=True);subprocess.run([str(binary)],check=True,timeout=5)
if __name__=='__main__':unittest.main()
