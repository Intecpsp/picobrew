"""Exercise portable native fixture compilation, loading and temporary cleanup."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import native_host as host


class NativeHostTests(unittest.TestCase):
    def test_platform_flags_and_extensions(self):
        for system, extension, shared, gc in (
            ('Windows', '.dll', '-shared', '-Wl,--gc-sections'),
            ('Darwin', '.dylib', '-dynamiclib', '-Wl,-dead_strip'),
            ('Linux', '.so', '-shared', '-Wl,--gc-sections'),
        ):
            with self.subTest(system=system), mock.patch.object(host, 'host_system', return_value=system):
                self.assertEqual(host.library_name('fixture'), 'fixture' + extension)
                self.assertIn(shared, host.shared_flags())
                self.assertEqual(host.gc_sections_flags(), [gc])
                self.assertEqual(host.executable_name('fixture'), 'fixture.exe' if system == 'Windows' else 'fixture')
                self.assertEqual(bool(host.pic_flags()), system != 'Windows')
                self.assertEqual(bool(host.sanitizer_flags()), system != 'Windows')
                self.assertEqual(host.sanitizer_flags(False), [])

    def test_compiler_path_with_spaces_is_one_executable(self):
        with mock.patch.dict(os.environ, {'CC': '/tools with spaces/compiler'}), \
                mock.patch.object(host.shutil, 'which', return_value='/tools with spaces/compiler') as which:
            self.assertEqual(host.compiler(), str(Path('/tools with spaces/compiler').resolve()))
            which.assert_called_once_with('/tools with spaces/compiler')

    def test_explicit_windows_compiler_directory_is_available_to_child(self):
        with mock.patch.object(host, 'host_system', return_value='Windows'), \
                mock.patch.object(host, '_compiler_directories', return_value=[]):
            compiler = Path('toolchain with spaces/bin/gcc.exe').resolve()
            env = host.runtime_environment({'PATH': 'existing', 'KEEP': 'yes'}, compiler_paths=[compiler])
            self.assertEqual(env['PATH'], str(compiler.parent) + os.pathsep + 'existing')
            self.assertEqual(env['KEEP'], 'yes')

    def test_real_c_and_cpp_libraries_load_and_are_removable(self):
        # This executes on each native CI host. In particular, Windows must
        # resolve the C++ runtime DLLs and unload our DLL before temp cleanup.
        with tempfile.TemporaryDirectory(prefix='pico native host ') as temporary:
            directory = Path(temporary)
            cases = [
                (False, 'c', 'int pico_fixture(void) { return 42; }\n'),
                (True, 'cpp', '#include <string>\nextern "C" int pico_fixture(void) { return (int)std::string("portable").size(); }\n'),
            ]
            for cxx, suffix, content in cases:
                source = directory / ('fixture.' + suffix)
                source.write_text(content, encoding='utf-8', newline='\n')
                library = directory / host.library_name('fixture-' + suffix)
                subprocess.run([host.compiler(cxx), *host.pic_flags(), *host.shared_flags(),
                                str(source), '-o', str(library)], check=True)
                with host.native_library(library) as loaded:
                    loaded.pico_fixture.restype = ctypes.c_int
                    self.assertEqual(loaded.pico_fixture(), 8 if cxx else 42)
                library.unlink()
                self.assertFalse(library.exists())


if __name__ == '__main__':
    unittest.main()
