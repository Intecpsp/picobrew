#!/usr/bin/env python3
"""Verify C model/display identity and reject mixed or stale native archives."""
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from machine_model import DISPLAYS, display_flag, model_flag, verify_archive_model
from native_host import compiler, library_name, pic_flags, shared_flags, load_library, close_library

SOURCES = [ROOT / name for name in (
    'tests/standalone/local_api_host.c', 'src/local_api.c', 'src/local_store.c',
    'src/local_history.c', 'src/local_telemetry.c', 'src/http_server.c')]


def archive_report(display):
    groups = {'source_sha256': (ROOT / 'src').rglob('*.c'),
              'header_sha256': (ROOT / 'include').rglob('*.h'),
              'platform_source_sha256': (p for p in (ROOT / 'platform').glob('*.cpp')
                                         if p.name != 'firmware_main.cpp')}
    return {'model': 'pico-c', 'display_variant': display, 'profile': 'standalone',
            **{name: {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in paths} for name, paths in groups.items()}}


class MachineIdentity(unittest.TestCase):
    def test_both_native_api_display_variants(self):
        for display in DISPLAYS:
            with self.subTest(display=display), tempfile.TemporaryDirectory() as temporary:
                target = Path(temporary) / library_name(display)
                subprocess.run([compiler(), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                *pic_flags(), *shared_flags(), '-O1', '-DPICO_STANDALONE=1',
                                model_flag('pico-c'), display_flag(display), '-I' + str(ROOT / 'include'),
                                *map(str, SOURCES), '-o', str(target)], check=True)
                library = load_library(target)
                try:
                    library.test_request.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                                                     ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
                    library.test_reset(1)
                    for route in ('/api/v1', '/api/v1/health', '/api/v1/status', '/api/v1/display'):
                        output = ctypes.create_string_buffer(4096)
                        code = library.test_request(b'GET', route.encode(), b'', 0, output, len(output))
                        self.assertEqual(code, 200)
                        data = json.loads(output.value)
                        self.assertEqual(data['display_variant'], display)
                        if route == '/api/v1/display':
                            self.assertEqual((data['width'], data['height'], data['rotation']),
                                             (128, 32, 0) if display == 'ssd1309' else (128, 64, 2))
                            self.assertEqual(data['storage_bytes'], 1024)
                            self.assertEqual(data['transfer_bytes'], 1024)
                            self.assertEqual(bytes.fromhex(data['data']), bytes(range(256)) * 4)
                        else:
                            self.assertEqual({key: data[key] for key in ('model', 'model_name', 'model_family')},
                                             {'model': 'pico-c', 'model_name': 'Pico C', 'model_family': 'pico-c'})
                        if route in ('/api/v1', '/api/v1/health'):
                            self.assertEqual(data['firmware_version'], '1.2.1')
                            self.assertEqual(data['api_version'], 1)
                        if route == '/api/v1':
                            self.assertIs(data['firmware_update'], False)
                finally:
                    close_library(library)

    def test_invalid_preprocessor_identity_is_rejected(self):
        for definition in ('PICO_MODEL=0', 'PICO_MODEL=1', 'PICO_MODEL=2', 'PICO_MODEL=99',
                           'PICO_C_DISPLAY_SSD1309=-1', 'PICO_C_DISPLAY_SSD1309=2'):
            with self.subTest(definition=definition):
                result = subprocess.run([compiler(), '-x', 'c', '-std=c11', '-fsyntax-only',
                                         '-I' + str(ROOT / 'include'), '-D' + definition, '-'],
                                        input='#include "pico_c/model.h"\n', text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)

    def test_build_cli_requires_supported_model_and_explicit_display(self):
        for tool in ('build.py', 'build_firmware.py', 'build_sdk_validation.py', 'package_hub_firmware.py'):
            for model in ('pico-pro', 'pico-s', 'z', 'zymatic'):
                with self.subTest(tool=tool, model=model):
                    result = subprocess.run([sys.executable, str(ROOT / 'tools' / tool),
                                             '--model', model, '--display', 'ssd1325'], text=True, capture_output=True)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn('invalid choice', result.stderr)
            result = subprocess.run([sys.executable, str(ROOT / 'tools' / tool), '--model', 'pico-c'],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('--display', result.stderr)

    def test_skip_archive_requires_matching_identity_and_all_current_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'identity is missing'):
                verify_archive_model(directory, 'pico-c', True, 'ssd1325')
            for display in DISPLAYS:
                report = archive_report(display)
                path = directory / 'build-report.json'
                path.write_text(json.dumps(report))
                verify_archive_model(directory, 'pico-c', True, display)
                for model, standalone, alternate in [('pico-pro', True, display), ('pico-c', False, display),
                                                     ('pico-c', True, 'ssd1309' if display == 'ssd1325' else 'ssd1325')]:
                    with self.assertRaisesRegex(ValueError, 'differs'):
                        verify_archive_model(directory, model, standalone, alternate)
                key = next(iter(report['source_sha256']))
                report['source_sha256'][key] = '0' * 64
                path.write_text(json.dumps(report))
                with self.assertRaisesRegex(ValueError, 'input changed'):
                    verify_archive_model(directory, 'pico-c', True, display)
                del report['source_sha256'][key]
                path.write_text(json.dumps(report))
                with self.assertRaisesRegex(ValueError, 'inventory'):
                    verify_archive_model(directory, 'pico-c', True, display)


if __name__ == '__main__':
    unittest.main()
