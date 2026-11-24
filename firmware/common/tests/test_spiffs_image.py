import hashlib
import json
import os
import secrets
import subprocess
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

    def test_usb_key_image_matches_pinned_native_formatter(self):
        import esp_provisioning
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for profile, geometry in spiffs.PROFILES.items():
                formatter = spiffs.compile_formatter(root/profile, self.esp8266, profile)
                image = root/'image'
                command = [str(formatter),str(geometry['partition_bytes']),str(geometry['block_bytes']),
                           str(geometry['partition_offset']),str(image)]
                subprocess.run(command, check=True, capture_output=True)
                empty = image.read_bytes()
                for _ in range(2):
                    key = secrets.token_hex(32)
                    source = root/'key'
                    source.write_bytes(key.encode())
                    result = subprocess.run(command+['/pb/api.key',str(source)],check=True,capture_output=True)
                    self.assertTrue(json.loads(result.stdout)['remount_verified'])
                    native = bytearray(image.read_bytes())
                    staged = esp_provisioning.stage(empty,profile,key)
                    # The C ABI declares three unused alignment bytes after the
                    # page header. Some compiler/SDK combinations leave them
                    # unspecified. The USB composer writes zeros there.
                    for offset in ([256,768] if profile.startswith('esp32') else [256]):
                        self.assertEqual(staged[offset+5:offset+8],bytes(3))
                        native[offset+5:offset+8]=bytes(3)
                    self.assertEqual(hashlib.sha256(native).digest(), hashlib.sha256(staged).digest())
    def test_z_repartition_remounts_preserved_files_and_rejects_unknown_storage(self):
        import esp_provisioning
        import struct
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected, pairs = validation.fixtures(root)
            binary=spiffs.compile_formatter(root/'tools', self.esp8266, 'esp32-4m')
            files=[part for item in pairs for part in item.split('=',1)]
            old, new, empty = root/'old', root/'new', root/'empty'
            subprocess.run([binary,'2097152','4096','2097152',old,*files],capture_output=True,check=True)
            subprocess.run([binary,'1503232','4096','2691072',empty],capture_output=True,check=True)
            source=bytearray(old.read_bytes())
            key=expected['/pb/api.key'].decode()
            contents=esp_provisioning._read_migration_image(source)
            header = next(p for p,(owner,span,_) in contents['pages'].items() if owner & 0x8000 and not span)
            struct.pack_into('<I',source,header*256+45,1731407803)
            updated=esp_provisioning.migrate_z_files(source,empty.read_bytes(),key,paired=True)
            new.write_bytes(updated)
            subprocess.run([binary,'--readback','1503232','4096','2691072',new,*files],check=True)
            self.assertEqual(esp_provisioning._read_migration_image(source)['metadata'],esp_provisioning._read_migration_image(updated)['metadata'])
            with self.assertRaisesRegex(ValueError,'pairing'):
                esp_provisioning.migrate_z_files(source,empty.read_bytes(),'a'*64,paired=True)
            source[252] ^= 1
            with self.assertRaisesRegex(ValueError,'magic'):
                esp_provisioning.migrate_z_files(source,empty.read_bytes(),key,paired=True)
            # An oversized, valid OEM directory must never be silently truncated.
            large=root/'large'
            large.write_bytes(b'a'*1_420_000)
            subprocess.run([binary,'2097152','4096','2097152',old,'/pb/large',large],capture_output=True,check=True)
            with self.assertRaisesRegex(ValueError,'capacity'):
                esp_provisioning.migrate_z_files(old.read_bytes(),empty.read_bytes(),key,paired=False)
            # An original filesystem can contain user files and no Hub key.
            src=root/'single'
            subprocess.run([binary,'2097152','4096','2097152',src,*files[:2]],capture_output=True,check=True)
            saved=esp_provisioning.migrate_z_files(src.read_bytes(),empty.read_bytes(),key,paired=False)
            self.assertEqual(esp_provisioning._read_migration_image(saved)['files'],
                             {files[0]: expected[files[0]], '/pb/api.key':key.encode()})

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
