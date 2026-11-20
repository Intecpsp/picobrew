"""Production Core ownership and alias wiring across real recovered components."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from program_recipe_native import dependency


class CoreApplicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='core-application-')
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        msp = directory / 'msp.o'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-I', str(ROOT / 'include'),
            '-c', str(ROOT / 'src/msp_protocol.c'), '-o', str(msp)], check=True)
        cls.binary = directory / 'core-application'
        sources = sorted((ROOT / 'src').glob('*.cpp')) + sorted((ROOT / 'adapters/arduinojson').glob('*.cpp'))
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++11', '-O1', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-fno-fast-math', '-ffp-contract=off', '-ffunction-sections', '-Wl,--gc-sections',
            '-I', str(ROOT / 'include'), '-I', str(dependency() / 'src'),
            str(ROOT / 'tests/core_application_integration.cpp')] + [str(path) for path in sources] +
            [str(msp), '-o', str(cls.binary)], check=True)

    def test_registration_uses_ready_and_retains_start_deadline(self):
        subprocess.run([str(self.binary), 'registration'], check=True)

    def test_command_storage_and_single_shared_recipe_mailbox(self):
        subprocess.run([str(self.binary), 'ownership'], check=True)

    def test_session_aliases_and_temperature_drop_lifecycle(self):
        subprocess.run([str(self.binary), 'sessions'], check=True)

    def test_still_wifi_lock_release_and_reconnect_order(self):
        subprocess.run([str(self.binary), 'still'], check=True)

    def test_serial_reader_dispatch_and_nested_owned_post(self):
        subprocess.run([str(self.binary), 'serial'], check=True)

    def test_msp_images_and_missing_application_bindings(self):
        subprocess.run([str(self.binary), 'images'], check=True)
        subprocess.run([sys.executable, str(ROOT / 'tools/recover_msp_images.py'), '--check'], check=True)

    def test_heater_critical_sections_end_before_response_io(self):
        subprocess.run([str(self.binary), 'snapshots'], check=True)

    def test_auxiliary_failures_preserve_core_continuation(self):
        subprocess.run([str(self.binary), 'auxiliary_failures'], check=True)

    def test_adc_failures_and_returning_assertions_preserve_core_continuation(self):
        subprocess.run([str(self.binary), 'adc_failures'], check=True)


if __name__ == '__main__':
    unittest.main()
