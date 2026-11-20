import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_spiffs_image as images
import spiffs_format as spiffs
import validate_spiffs_format as validation


class SpiffsImageEvidenceTests(unittest.TestCase):
    def test_current_provenance_and_exact_sdk_layout(self):
        report = json.loads((ROOT / 'analysis/spiffs-image-validation.json').read_text())
        self.assertTrue(report['passed'])
        for name, expected in report['source_sha256'].items():
            self.assertEqual(spiffs.digest(ROOT / name), expected, name)
        self.assertEqual(report['esp32_dwarf_witness']['layout']['object_header']['size'], 49)
        for profile in report['profiles'].values():
            self.assertTrue(profile['deterministic'])
            self.assertTrue(profile['empty']['native']['remount_verified'])
            self.assertTrue(profile['staged']['independent_decode']['exact_contents'])

    def test_namespace_reserves_transaction_paths(self):
        self.assertTrue(spiffs.valid_logical_name('/pb/api.key'))
        self.assertTrue(spiffs.valid_logical_name('/pb/' + 'a' * 23))
        for name in ['/pb/' + 'a' * 24, '/pb/../api.key', '/pb/api.key.tmp', '/pb/api.key.bak', '/pb/a/b']:
            self.assertFalse(spiffs.valid_logical_name(name), name)


class SpiffsImageNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.esp8266 = os.environ.get('PICO_ESP8266_SDK')
        cls.esp32 = os.environ.get('PICO_ESP32_SDK')
        if not cls.esp8266 or not cls.esp32:
            raise unittest.SkipTest('Set PICO_ESP8266_SDK and PICO_ESP32_SDK for actual native filesystem formatting')
        cls.esp8266, cls.esp32 = Path(cls.esp8266), Path(cls.esp32)

    def test_both_profiles_remount_staged_multi_index_objects(self):
        with tempfile.TemporaryDirectory(prefix='picobrew-spiffs-image-test-') as temporary:
            directory = Path(temporary)
            expected, arguments = validation.fixtures(directory)
            for profile in spiffs.PROFILES:
                output = directory / (profile + '.bin')
                report = images.build(profile, self.esp8266, self.esp32, output, arguments)
                self.assertEqual(report['native']['files'], len(expected))
                self.assertTrue(report['native']['remount_verified'])
                self.assertTrue(report['independent_decode']['exact_contents'])
                self.assertEqual(output.stat().st_mode & 0o077, 0)

    def test_invalid_explicit_input_preserves_existing_output(self):
        with tempfile.TemporaryDirectory(prefix='picobrew-spiffs-input-test-') as temporary:
            directory = Path(temporary)
            output, invalid = directory / 'image.bin', directory / 'key'
            output.write_bytes(b'preserve-existing-artifact')
            invalid.write_bytes(b'not-a-key')
            with self.assertRaises(ValueError):
                images.build('esp32-4m', self.esp8266, self.esp32, output, ['/pb/api.key=' + str(invalid)])
            self.assertEqual(output.read_bytes(), b'preserve-existing-artifact')


if __name__ == '__main__':
    unittest.main()
