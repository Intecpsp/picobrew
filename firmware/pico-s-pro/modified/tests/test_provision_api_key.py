#!/usr/bin/env python3
"""Native private key creation and copied-tool dependency checks; no hardware."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import provision_api_key as provision


class ProvisionKeyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_key_is_private_exact_lf_and_existing_key_is_untouched(self):
        path = self.root / "private" / "api.key"
        self.assertEqual(provision.create_key(path), path)
        contents = provision.private_files.read_regular(path, 65, private=True)
        self.assertRegex(contents, rb"\A[0-9a-f]{64}\n\Z")
        provision.private_files.assert_private(path.parent, directory=True)
        with self.assertRaises(FileExistsError):
            provision.create_key(path)
        self.assertEqual(path.read_bytes(), contents)

    def test_copied_tool_requires_shared_helper_then_runs_with_it(self):
        scripts = self.root / "scripts"
        scripts.mkdir()
        for name in ["provision_api_key.py", "pico_private_files.py"]:
            shutil.copyfile(ROOT / "tools" / name, scripts / name)
        output = self.root / "private" / "api.key"
        command = [sys.executable, str(scripts / "provision_api_key.py"), "--output", str(output)]
        failed = subprocess.run(command, cwd=scripts, text=True, capture_output=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("Missing private_files.py", failed.stderr)
        self.assertFalse(output.exists())
        shutil.copyfile(ROOT.parents[2] / "tools" / "private_files.py", scripts / "private_files.py")
        succeeded = subprocess.run(command, cwd=scripts, text=True, capture_output=True)
        self.assertEqual(succeeded.returncode, 0, succeeded.stdout + succeeded.stderr)
        provision.private_files.assert_private(output)
        self.assertNotIn(output.read_text().strip(), succeeded.stdout + succeeded.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
