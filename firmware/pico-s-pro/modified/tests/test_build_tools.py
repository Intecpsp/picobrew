"""Cross-platform compiler discovery and pinned archive installation checks."""

import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import stat
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from arm_toolchain import compiler_prefix, find_arm_prefix, sort_sources
from bootstrap_dependencies import compiler_for_host, extract_zip, install


class CompilerHostTests(unittest.TestCase):
    def setUp(self):
        self.lock = json.loads((ROOT / 'dependencies/lock.json').read_text(encoding='utf-8'))
        self.hosts = json.loads((ROOT / 'dependencies/host-toolchains.json').read_text(encoding='utf-8'))

    def test_linux_keeps_the_exact_existing_baseline_entry(self):
        self.assertIs(compiler_for_host(self.lock, self.hosts, 'Linux', 'x86_64'), self.lock['toolchain'])

    def test_windows_and_macos_select_their_own_archive_identity(self):
        windows = compiler_for_host(self.lock, self.hosts, 'Windows', 'AMD64')
        mac = compiler_for_host(self.lock, self.hosts, 'Darwin', 'x86_64')
        silicon = compiler_for_host(self.lock, self.hosts, 'Darwin', 'arm64')
        self.assertTrue(windows['archive_filename'].endswith('-win32.zip'))
        self.assertTrue(mac['archive_filename'].endswith('-mac.tar.bz2'))
        self.assertNotEqual(windows['sha256'], self.lock['toolchain']['sha256'])
        self.assertNotEqual(mac['sha256'], self.lock['toolchain']['sha256'])
        self.assertIs(mac, silicon)
        self.assertIn('Rosetta 2', silicon['compatibility_note'])

    def test_unknown_host_cannot_silently_get_a_linux_compiler(self):
        with self.assertRaisesRegex(RuntimeError, '--sdk-only'):
            compiler_for_host(self.lock, self.hosts, 'Windows', 'ARM64')

    def test_executable_suffix_is_not_part_of_the_tool_prefix(self):
        for executable in ('/tools/arm-none-eabi-gcc', r'C:\Program Files\Arm\arm-none-eabi-gcc.exe',
                           r'C:\Program Files\Arm\arm-none-eabi-gcc.EXE'):
            prefix = compiler_prefix(executable)
            self.assertTrue(prefix.endswith('arm-none-eabi-'))
            self.assertNotIn('.exe', (prefix + 'ar').lower())

    def test_local_windows_package_is_found_without_path_changes(self):
        with tempfile.TemporaryDirectory(prefix='pico compiler ') as directory:
            project = Path(directory)
            compiler = project / '.deps/toolchain/bin/arm-none-eabi-gcc.exe'
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b'MZ test fixture')
            with mock.patch('arm_toolchain.shutil.which', return_value=None):
                self.assertEqual(find_arm_prefix(project), str(project / '.deps/toolchain/bin/arm-none-eabi-'))

    def test_build_input_order_keeps_posix_case_and_component_order(self):
        names = ['src/network.c', 'src/network/packets.c', 'src/z.c', 'src/A.c', 'src/a.c']
        expected = [path.as_posix() for path in sorted(PurePosixPath(name) for name in names)]
        self.assertEqual([path.as_posix() for path in sort_sources(PureWindowsPath(name) for name in names)], expected)
        self.assertEqual([path.as_posix() for path in sort_sources(PurePosixPath(name) for name in names)], expected)


class CompilerArchiveTests(unittest.TestCase):
    def test_zip_installs_regular_files_and_records_actual_archive(self):
        with tempfile.TemporaryDirectory(prefix='pico zip ') as directory:
            prefix = Path(directory)
            archives = prefix / 'archives'
            archives.mkdir()
            archive = archives / 'windows.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('compiler/bin/arm-none-eabi-gcc.exe', b'MZ fixture')
            entry = {
                'name': 'Windows compiler fixture', 'directory': 'toolchain',
                'archive_filename': archive.name, 'archive_root': 'compiler',
                'url': 'https://example.invalid/pinned.zip',
                'bytes': archive.stat().st_size,
                'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
            }
            destination = install(entry, prefix)
            self.assertEqual((destination / 'bin/arm-none-eabi-gcc.exe').read_bytes(), b'MZ fixture')
            receipt = json.loads((destination / '.picobrew-dependency.json').read_text(encoding='utf-8'))
            self.assertEqual(receipt['archive_sha256'], entry['sha256'])
            self.assertEqual(receipt['url'], entry['url'])
            self.assertEqual(install(entry, prefix), destination)
            with self.assertRaisesRegex(RuntimeError, 'different identity'):
                install(entry | {'sha256': '0' * 64}, prefix)

    def test_zip_rejects_traversal_and_windows_drive_paths(self):
        for name in ('../escape', '/absolute', r'folder\..\escape', 'C:/escape',
                     'folder/../../escape', 'folder/./escape', 'folder//escape'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / 'input.zip'
                target = root / 'output'
                target.mkdir()
                with zipfile.ZipFile(archive, 'w') as output:
                    output.writestr(name, b'not written')
                with self.assertRaisesRegex(RuntimeError, 'Unsafe compiler archive entry'):
                    extract_zip(archive, target)
                self.assertEqual(list(target.iterdir()), [])

    def test_zip_rejects_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, target = root / 'input.zip', root / 'output'
            target.mkdir()
            link = zipfile.ZipInfo('compiler/link')
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr(link, '../../escape')
            with self.assertRaisesRegex(RuntimeError, 'Unsafe compiler archive entry'):
                extract_zip(archive, target)
            self.assertEqual(list(target.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
