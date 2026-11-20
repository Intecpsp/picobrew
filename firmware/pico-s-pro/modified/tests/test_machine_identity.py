#!/usr/bin/env python3
"""Verify real API identity for both compiled variants and reject mismatched builds."""
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from machine_model import MODELS, model_flag, verify_archive_model
from native_host import compiler, library_name, pic_flags, shared_flags, load_library, close_library

SOURCES = [ROOT / name for name in (
    'tests/standalone/local_api_host.c', 'src/local_api.c', 'src/local_store.c',
    'src/local_history.c', 'src/local_telemetry.c', 'src/http_server.c')]


class MachineIdentity(unittest.TestCase):
    def test_both_native_api_variants(self):
        for model, name in [('pico-pro', 'Pico Pro'), ('pico-s', 'Pico S')]:
            with self.subTest(model=model), tempfile.TemporaryDirectory() as temporary:
                target = Path(temporary) / library_name(model)
                subprocess.run([compiler(), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                *pic_flags(), *shared_flags(), '-O1', '-DPICO_STANDALONE=1',
                                model_flag(model), '-I' + str(ROOT / 'include'),
                                *map(str, SOURCES), '-o', str(target)], check=True)
                library = load_library(target)
                try:
                    library.test_request.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                                                     ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
                    library.test_reset(1)
                    for route in ('/api/v1', '/api/v1/health', '/api/v1/status'):
                        output = ctypes.create_string_buffer(4096)
                        code = library.test_request(b'GET', route.encode(), b'', 0, output, len(output))
                        self.assertEqual(code, 200)
                        data = json.loads(output.value)
                        self.assertEqual({key: data[key] for key in ('model', 'model_name', 'model_family')},
                                         {'model': model, 'model_name': name, 'model_family': 'pico-s-pro'})
                        if route != '/api/v1/status':
                            self.assertEqual(data['firmware_version'], '1.3.1')
                            self.assertEqual(data['api_version'], 1)
                finally:
                    close_library(library)

    def test_invalid_preprocessor_model_is_rejected(self):
        for choice in ('0', '99', 'PICO_MODEL_PICO_C'):
            with self.subTest(choice=choice):
                result = subprocess.run([compiler(), '-x', 'c', '-std=c11', '-fsyntax-only',
                                         '-I' + str(ROOT / 'include'), '-DPICO_MODEL=' + choice, '-'],
                                        input='#include "picobrew/model.h"\n', text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Unsupported PICO_MODEL', result.stderr)

    def test_unsupported_build_cli_model_is_rejected(self):
        for tool in ('build.py', 'build_firmware.py', 'build_sdk_validation.py'):
            for model in ('pico-c', 'z', 'zymatic'):
                with self.subTest(tool=tool, model=model):
                    result = subprocess.run([sys.executable, str(ROOT / 'tools' / tool),
                                             '--standalone', '--model', model], text=True, capture_output=True)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn('invalid choice', result.stderr)

    def test_skip_archive_requires_matching_model_and_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'identity is missing'):
                verify_archive_model(root, 'pico-pro', True)
            for model in MODELS:
                (root / 'build-report.json').write_text(json.dumps({'model': model, 'profile': 'standalone'}))
                verify_archive_model(root, model, True)
                other = 'pico-pro' if model == 'pico-s' else 'pico-s'
                with self.assertRaisesRegex(ValueError, 'differs'):
                    verify_archive_model(root, other, True)
                with self.assertRaisesRegex(ValueError, 'differs'):
                    verify_archive_model(root, model, False)


if __name__ == '__main__':
    unittest.main()
