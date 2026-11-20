"""Common browser asset integrity, presentation protocol and streaming callback tests."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CommonWebTests(unittest.TestCase):
    def test_generated_assets_are_current_and_deterministic(self):
        subprocess.run(['python3', str(ROOT / 'tools/embed_web.py'), '--check'], check=True)
        manifest = json.loads((ROOT / 'firmware/web/manifest.json').read_text())
        self.assertEqual(len(manifest['assets']), 6)
        for entry in manifest['assets']:
            original = (ROOT / 'web' / entry['source']).read_bytes()
            compressed = (ROOT / 'firmware/web' / (entry['source'] + '.gz')).read_bytes()
            self.assertEqual(gzip.decompress(compressed), original)
            self.assertEqual(hashlib.sha256(original).hexdigest(), entry['source_sha256'])
            self.assertEqual(hashlib.sha256(compressed).hexdigest(), entry['sha256'])
            self.assertEqual(compressed[4:8], b'\0' * 4)

    def test_display_and_capability_protocol(self):
        subprocess.run(['node', '--test', str(ROOT / 'tests/web.test.cjs')], check=True)

    def test_native_asset_stream_preserves_every_compressed_byte(self):
        with tempfile.TemporaryDirectory(prefix='pico-common-web-') as directory:
            binary = str(Path(directory) / 'web-assets')
            subprocess.run(['g++', '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
                '-I' + str(ROOT / 'include'), str(ROOT / 'tests/web_assets.cpp'),
                str(ROOT / 'src/common_web_assets.cpp'), '-o', binary], check=True)
            subprocess.run([binary, directory], check=True)
            names = ['index.html', 'index.html', 'device.js', 'app.js', 'api-explorer.js', 'history.js', 'style.css']
            for index, name in enumerate(names):
                self.assertEqual((Path(directory) / f'asset-{index}.gz').read_bytes(),
                    (ROOT / 'firmware/web' / (name + '.gz')).read_bytes())

if __name__ == '__main__':
    unittest.main()
