"""Actual modified PicoFerm controller + shared Engine, without physical I/O."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
OEM = ROOT.parent / 'reverse-engineered'
COMMON = ROOT.parents[1] / 'common'


class StandaloneTests(unittest.TestCase):
    def build_run(self, sanitizer):
        flags = ['-O1' if sanitizer else '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic', '-ffunction-sections', '-fdata-sections']
        if sanitizer:
            flags += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie']
        includes = ['-I'+str(path) for path in (ROOT/'include', OEM/'include', COMMON/'include')]
        with tempfile.TemporaryDirectory(prefix='ferm-modified-test-') as folder:
            directory = Path(folder)
            transport = directory / 'http.o'
            subprocess.run(['gcc', '-std=c11', *flags, *includes, '-c', str(COMMON/'src/http_server.c'), '-o', str(transport)], check=True)
            sources = sorted((ROOT/'src').glob('*.cpp')) + [OEM/'src/core.cpp', OEM/'src/hardware.cpp', ROOT/'tests/standalone_test.cpp']
            sources += [COMMON/'src'/name for name in ('engine.cpp', 'api.cpp', 'values.cpp', 'records.cpp', 'history.cpp', 'history_records.cpp', 'telemetry.cpp')]
            executable = directory / 'standalone'
            subprocess.run(['g++', '-std=c++11', *flags, *includes, *map(str,sources), str(transport), '-Wl,--gc-sections', *(['-no-pie'] if sanitizer else []), '-o', str(executable)], check=True)
            # The managed executor cannot run LeakSanitizer's process tracer.
            # ASan/UBSan remain active; callers may opt into LSan elsewhere.
            environment = dict(os.environ, ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS','detect_leaks=0'), UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            subprocess.run([str(executable)], check=True, env=environment, timeout=30)
            symbols = subprocess.check_output(['nm', '-C', str(executable)], text=True)
            functions = '\n'.join(line for line in symbols.splitlines() if len(line.split())>=3 and line.split()[1] in ('T','t','W','w'))
            for forbidden in ('picoferm::oem::Adc::wait_', 'picoferm::oem::Adc::sample()', 'picoferm::oem::Acquisition::advance('):
                self.assertFalse(forbidden in functions, 'Unbounded OEM function retained: '+forbidden)

    def test_controller_engine_and_bounded_acquisition(self):
        self.build_run(False)

    def test_native_sanitizers(self):
        self.build_run(True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
