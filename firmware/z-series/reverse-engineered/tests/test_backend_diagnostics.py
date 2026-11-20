import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from program_recipe_native import dependency
from analyze_images import parse_image


class BackendDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='z-backend-diagnostics-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / 'probe'
        sources = ['tests/backend_diagnostics_probe.cpp', 'src/backend_diagnostics.cpp',
                   'src/program_repository.cpp', 'src/enums.cpp']
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) +
            ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
             '-ffunction-sections', '-Wl,--gc-sections', '-I', str(ROOT / 'include'),
             '-I', str(dependency() / 'src')] + [str(ROOT / p) for p in sources] +
            ['-o', str(cls.binary)], check=True)
        cls.evidence = json.loads((ROOT / 'analysis/backend-diagnostics-validation.json').read_text())

    def run_cases(self, cases):
        output = subprocess.run([str(self.binary)], input=''.join(json.dumps(c)+'\n' for c in cases),
                                text=True, capture_output=True, check=True)
        results = [json.loads(line) for line in output.stdout.splitlines()]
        self.assertEqual(len(results), len(cases))
        return results

    def test_original_literal_provenance_and_rendering(self):
        image = (ROOT.parent / 'oem/upstream/zseries_0_0_119.bin').read_bytes()
        self.assertEqual(hashlib.sha256(image).hexdigest(), self.evidence['image_sha256'])
        segments = parse_image(image)['segments']
        for row in self.evidence['literals']:
            address = int(row['address'], 16)
            segment = next(s for s in segments if s['load_address'] <= address < s['load_address']+s['size_bytes'])
            offset = segment['file_offset'] + address - segment['load_address']
            self.assertEqual(image[offset:offset+len(row['text'].encode())], row['text'].encode(), row)
        cases = [row['input'] for row in self.evidence['native_cases']]
        for row, result in zip(self.evidence['native_cases'], self.run_cases(cases)):
            with self.subTest(input=row['input']):
                self.assertEqual(row['expected'], result)

    def test_missing_services_and_unknown_lines_fail_explicitly(self):
        cases = [dict(op='assertion', domain=0, line=0xe7, missing=True),
                 dict(domain=0, line=0xaf, missing=True), dict(domain=0, line=0x350),
                 dict(domain=2, line=0xca), dict(domain=0, line=0xe0, error=77)]
        cases += [dict(op=op, domain=domain, line=0xffffffff)
                  for op in ['assertion', 'diagnostic', 'description'] for domain in range(3)]
        self.assertEqual(self.run_cases(cases), [dict(ok=False, events=[])] * len(cases))

    def test_checked_source_and_target_provenance(self):
        for path, sha in self.evidence['source_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT/path).read_bytes()).hexdigest(), sha, path)
        target = json.loads((ROOT/'analysis/backend-diagnostics-target-build.json').read_text())
        self.assertEqual(len(target['sources']), 1)
        for row in target['sources']:
            self.assertEqual(hashlib.sha256((ROOT/row['source']).read_bytes()).hexdigest(), row['source_sha256'])


if __name__ == '__main__':
    unittest.main()
