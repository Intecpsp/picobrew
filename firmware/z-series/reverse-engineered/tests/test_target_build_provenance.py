"""Reject stale archive inputs before a link can publish their provenance."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
SPEC = importlib.util.spec_from_file_location('z_target_link_provenance', ROOT / 'tools/link_esp32_application.py')
link = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(link)


class TargetProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='z-target-provenance-')
        self.addCleanup(self.directory.cleanup)
        self.repository = Path(self.directory.name)
        self.root = self.repository / 'firmware/z-series/reverse-engineered'
        self.objects = self.repository / 'objects'
        self.objects.mkdir()
        old_root, old_repository = link.ROOT, link.REPOSITORY
        self.addCleanup(lambda: setattr(link, 'ROOT', old_root))
        self.addCleanup(lambda: setattr(link, 'REPOSITORY', old_repository))
        link.ROOT, link.REPOSITORY = self.root, self.repository

        def file(path, data=b'verified input\n'):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            return link.digest(path)

        self.report = {'sources': [], 'ethernet2_objects': {}}
        source = 'src/core.cpp'
        self.report['sources'].append(dict(source=source, source_sha256=file(self.root / source),
            object_sha256=file(self.objects / 'src_core.cpp.o', b'compiled object')))
        for key, name in (
            ('owned_header_sha256', 'include/zseries/core.hpp'),
            ('platform_header_sha256', 'platform/esp32/device.hpp'),
            ('adapter_header_sha256', 'adapters/arduinojson/recipe_document.hpp'),
            ('implementation_include_sha256', 'src/implementation.inc')):
            self.report[key] = {name: file(self.root / name)}
        self.report['build_input_sha256'] = {}
        for name in ('tools/build_esp32_objects.py', 'tools/recover_backend_credentials.py', 'dependencies/esp32.json'):
            path = self.root / name
            self.report['build_input_sha256'][path.relative_to(self.repository).as_posix()] = file(path)
        file(self.root / 'tools/link_esp32_application.py', Path(link.__file__).read_bytes())
        self.report['archive_sha256'] = file(self.objects / 'libzseries_recovered_esp32.a', b'archive')
        self.report['original_backend_constants'] = dict(
            generated_header_sha256=file(self.objects / 'generated/oem_credentials.hpp', b'fixture only'),
            generator_sha256=link.digest(self.root / 'tools/recover_backend_credentials.py'))
        self.write_report()

    def write_report(self):
        (self.objects / 'build-report.json').write_text(json.dumps(self.report))

    def test_final_report_carries_all_compiled_and_generator_inputs(self):
        verified = link.verify_objects(self.objects)
        sources, headers = link.reconstructed_provenance(verified)
        self.assertEqual(sources, [dict(source='firmware/z-series/reverse-engineered/src/core.cpp',
            source_sha256=link.digest(self.root / 'src/core.cpp'))])
        required = [*self.report['owned_header_sha256'], *self.report['platform_header_sha256'],
                    *self.report['adapter_header_sha256'], *self.report['implementation_include_sha256'],
                    'tools/build_esp32_objects.py', 'tools/recover_backend_credentials.py',
                    'dependencies/esp32.json', 'tools/link_esp32_application.py']
        for name in required:
            full = 'firmware/z-series/reverse-engineered/' + name
            self.assertEqual(headers[full], link.digest(self.root / name))
        self.assertTrue(all(not Path(name).is_absolute() and '..' not in Path(name).parts for name in headers))

    def test_reuse_rejects_changed_source_header_adapter_or_generator(self):
        for name in ('src/core.cpp', 'include/zseries/core.hpp', 'platform/esp32/device.hpp',
                     'adapters/arduinojson/recipe_document.hpp', 'src/implementation.inc',
                     'tools/build_esp32_objects.py', 'tools/recover_backend_credentials.py',
                     'dependencies/esp32.json'):
            with self.subTest(input=name):
                path = self.root / name
                original = path.read_bytes()
                path.write_bytes(original + b'changed\n')
                with self.assertRaises(ValueError):
                    link.verify_objects(self.objects)
                path.write_bytes(original)

    def test_legacy_incomplete_object_provenance_requires_rebuild(self):
        del self.report['build_input_sha256']
        self.write_report()
        with self.assertRaisesRegex(ValueError, 'Rebuild reconstructed target objects'):
            link.verify_objects(self.objects)


if __name__ == '__main__':
    unittest.main()
