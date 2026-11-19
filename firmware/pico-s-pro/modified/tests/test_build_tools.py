"""Cross-platform compiler discovery and pinned archive installation checks."""

from contextlib import chdir
import hashlib
import io
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import stat
import shutil
import subprocess
import sys
import tempfile
import tarfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from arm_toolchain import compiler_prefix, find_arm_prefix, sort_sources
from bootstrap_dependencies import compiler_for_host, extract_zip, install, install_target_runtime
from build_sdk_validation import read_archive_members, sdk_include_dirs
from native_host import compiler


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

    def test_library_header_wins_over_case_insensitive_driverlib_collision(self):
        with tempfile.TemporaryDirectory(prefix='pico header search ') as directory:
            root = Path(directory)
            sdk = root / 'sdk'
            library = sdk / 'libraries/SPI'
            driver = sdk / 'system/driverlib'
            library.mkdir(parents=True)
            driver.mkdir(parents=True)
            # Add both aliases to reproduce Windows/macOS lookup on a
            # case-sensitive Linux checkout too (identical writes on Windows).
            for name in ('SPI.h', 'spi.h'):
                (library / name).write_text('extern int SPI;\n', encoding='utf-8')
                (driver / name).write_text('#define DRIVER_SPI_BASE 0\n', encoding='utf-8')
            for cxx, header, symbol in [(True, 'SPI.h', 'SPI'), (False, 'spi.h', 'DRIVER_SPI_BASE')]:
                source = root / ('probe.cpp' if cxx else 'probe.c')
                source.write_text(f'#include <{header}>\nint probe() {{ return {symbol}; }}\n', encoding='utf-8')
                subprocess.run([compiler(cxx=cxx), '-fsyntax-only',
                                *['-I' + str(p) for p in sdk_include_dirs(sdk, root, cxx=cxx)], str(source)], check=True)


class CompilerArchiveTests(unittest.TestCase):
    def test_native_ar_extraction_preserves_binary_member_bytes(self):
        with tempfile.TemporaryDirectory(prefix='pico archive bytes ') as directory:
            root = Path(directory)
            archiver = subprocess.check_output([compiler(), '-print-prog-name=ar'], text=True).strip()
            source = root / 'binary.c'
            member = root / 'binary.o'
            # A real native object also works with the macOS archive indexer,
            # while the payload covers LF, CRLF, Ctrl-Z, NUL and high bytes.
            payload = b'\x00\n\r\n\x1a\xff' + bytes(range(256))
            source.write_text('const unsigned char pico_binary_data[] = {' +
                              ','.join(str(value) for value in payload) + '};\n', encoding='utf-8')
            subprocess.run([compiler(), '-c', str(source), '-o', str(member)], check=True)
            data = member.read_bytes()
            self.assertIn(payload, data)
            archive = root / 'fixture.a'
            subprocess.run([archiver, 'rcs', str(archive), str(member)], check=True)
            member.unlink()
            self.assertEqual(read_archive_members(archiver, archive, [member.name]), {member.name: data})
            resolved_ar = Path(shutil.which(archiver)).resolve()
            with chdir(resolved_ar.parent):
                self.assertEqual(read_archive_members('./' + resolved_ar.name, archive, [member.name]), {member.name: data})
            with chdir(root):
                self.assertEqual(read_archive_members(str(resolved_ar), Path(archive.name), [member.name]), {member.name: data})
            with self.assertRaises((FileNotFoundError, subprocess.CalledProcessError)):
                read_archive_members(archiver, archive, ['missing.o'])

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


class TargetRuntimeTests(unittest.TestCase):
    def fixture(self, directory):
        prefix = Path(directory)
        compiler = prefix / 'toolchain'
        paths = ['arm-none-eabi/lib/thumb/v7e-m/nofp/libc.a',
                 'lib/gcc/arm-none-eabi/10.3.1/thumb/v7e-m/nofp/libgcc.a']
        files, originals, expected = [], {}, {}
        for index, relative in enumerate(paths):
            original = b'original host runtime ' + bytes([index])
            canonical = b'!<arch>\ncanonical\x00\r\n\x1a' + bytes([index])
            target = compiler / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original)
            originals[relative] = hashlib.sha256(original).hexdigest()
            expected[relative] = canonical
            files.append({'path': relative, 'bytes': len(canonical),
                          'sha256': hashlib.sha256(canonical).hexdigest()})
        executable = compiler / 'bin/arm-none-eabi-gcc'
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b'native host executable')
        (prefix / 'archives').mkdir()
        archive = prefix / 'archives/canonical.tar.bz2'
        with tarfile.open(archive, 'w:bz2') as output:
            for index, (relative, data) in enumerate(expected.items()):
                shared = f'package/shared/runtime-{index}.a'
                info = tarfile.TarInfo(shared)
                info.size = len(data)
                output.addfile(info, io.BytesIO(data))
                link = tarfile.TarInfo('package/' + relative)
                link.type = tarfile.LNKTYPE
                link.linkname = shared
                output.addfile(link)
        source = {'name': 'canonical runtime fixture', 'archive_root': 'package',
                  'archive_filename': archive.name, 'url': 'https://example.invalid/canonical.tar.bz2',
                  'bytes': archive.stat().st_size, 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()}
        host = {'sha256': 'a' * 64, 'target_runtime_original_sha256': originals}
        host_lock = {'target_runtime': {'multilib': 'thumb/v7e-m/nofp', 'reason': 'fixture', 'files': files}}
        return prefix, compiler, {'toolchain': source}, host_lock, host, expected

    def test_known_host_runtime_is_replaced_and_receipt_distinguishes_sources(self):
        with tempfile.TemporaryDirectory(prefix='pico canonical runtime ') as directory:
            prefix, compiler, lock, host_lock, host, expected = self.fixture(directory)
            result = install_target_runtime(lock, host_lock, host, prefix, compiler)
            for relative, data in expected.items():
                self.assertEqual((compiler / relative).read_bytes(), data)
            self.assertEqual((compiler / 'bin/arm-none-eabi-gcc').read_bytes(), b'native host executable')
            self.assertEqual(result['host_compiler_archive_sha256'], host['sha256'])
            self.assertEqual(result['target_runtime_source_archive_sha256'], lock['toolchain']['sha256'])
            with mock.patch('bootstrap_dependencies.pinned_archive', side_effect=AssertionError('already canonical')):
                self.assertEqual(install_target_runtime(lock, host_lock, host, prefix, compiler), result)

    def test_matching_host_runtime_needs_no_download_and_records_its_real_source(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix, compiler, lock, host_lock, host, expected = self.fixture(directory)
            for relative, data in expected.items():
                (compiler / relative).write_bytes(data)
            del host['target_runtime_original_sha256']
            with mock.patch('bootstrap_dependencies.pinned_archive', side_effect=AssertionError('already canonical')):
                result = install_target_runtime(lock, host_lock, host, prefix, compiler)
            self.assertEqual(result['target_runtime_source_archive_sha256'], host['sha256'])
            self.assertEqual(result['canonical_reference_archive_sha256'], lock['toolchain']['sha256'])

    def test_unknown_runtime_or_bad_canonical_hash_cannot_overwrite_files(self):
        for failure in ('unknown existing', 'wrong canonical hash'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                prefix, compiler, lock, host_lock, host, expected = self.fixture(directory)
                target = compiler / next(iter(expected))
                if failure == 'unknown existing':
                    target.write_bytes(b'unrecognized user content')
                else:
                    host_lock['target_runtime']['files'][0]['sha256'] = '0' * 64
                before = {relative: (compiler / relative).read_bytes() for relative in expected}
                with self.assertRaisesRegex(RuntimeError, 'unexpected target runtime|hash differs'):
                    install_target_runtime(lock, host_lock, host, prefix, compiler)
                self.assertEqual({relative: (compiler / relative).read_bytes() for relative in expected}, before)
                self.assertFalse((compiler / '.picobrew-target-runtime.json').exists())


if __name__ == '__main__':
    unittest.main()
