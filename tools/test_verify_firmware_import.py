"""Preserve imported bytes and Git modes on every checkout platform."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import verify_firmware_import as verifier


@unittest.skipUnless(shutil.which("git"), "Git is required to test index-backed mode verification")
class ImportVerificationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="picobrew import ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        patch = mock.patch.object(verifier, "ROOT", self.root)
        patch.start()
        self.addCleanup(patch.stop)
        self.git("init", "--quiet")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.filemode", "false")
        self.relative = "firmware/pico-s-pro/reverse-engineered/café.py"
        self.file = self.root / self.relative
        self.file.parent.mkdir(parents=True)
        self.contents = "# Original café source\nprint('hello')\n".encode("utf-8")
        self.file.write_bytes(self.contents)
        self.file.chmod(0o644)
        self.git("add", "--", self.relative)
        self.git("update-index", "--chmod=+x", "--", self.relative)
        blob = verifier.git_hash("blob", self.contents)
        manifest = {
            "source_root": "source",
            "source_tree": verifier.tree_hash({"café.py": ("100755", blob)}),
            "files": [{
                "source_path": "source/café.py", "destination_path": self.relative,
                "size": len(self.contents), "sha256": hashlib.sha256(self.contents).hexdigest(),
                "git_blob_sha": blob, "mode": "100755",
            }],
        }
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], stderr=subprocess.PIPE)

    def verify_index(self):
        return verifier.verify_manifest(self.manifest, index_modes=verifier.git_index_modes(self.root))

    def test_index_verifies_executable_mode_without_filesystem_bit(self):
        result = self.verify_index()
        self.assertTrue(result["verified"])
        self.assertEqual(result["mode_evidence"], "git-index")

    def test_changed_index_mode_is_rejected(self):
        self.git("update-index", "--chmod=-x", "--", self.relative)
        with self.assertRaisesRegex(ValueError, "Imported bytes or mode changed"):
            self.verify_index()

    def test_changed_worktree_bytes_are_rejected_even_when_index_matches(self):
        self.file.write_bytes(self.contents + b"# changed\n")
        with self.assertRaisesRegex(ValueError, "Imported bytes or mode changed"):
            self.verify_index()

    def test_untracked_file_is_not_given_an_assumed_mode(self):
        self.git("rm", "--cached", "--", self.relative)
        with self.assertRaisesRegex(ValueError, "missing from the Git index"):
            self.verify_index()

    def test_parent_repository_does_not_supply_modes_for_a_source_export(self):
        exported = self.root / "export"
        exported.mkdir()
        with self.assertRaisesRegex(ValueError, "full Git checkout"):
            verifier.git_index_modes(exported)

    @unittest.skipIf(os.name == "nt", "Windows has no POSIX executable mode")
    def test_posix_still_checks_actual_filesystem_mode(self):
        with self.assertRaisesRegex(ValueError, "Imported bytes or mode changed"):
            verifier.verify_manifest(self.manifest)


@unittest.skipUnless(shutil.which("git"), "Git is required to test checkout attributes")
class CheckoutBytePreservationTests(unittest.TestCase):
    def test_autocrlf_keeps_preserved_bytes_and_active_text_lf(self):
        attributes = Path(__file__).resolve().parents[1] / ".gitattributes"
        with tempfile.TemporaryDirectory(prefix="picobrew checkout ") as temporary:
            root = Path(temporary)

            def git(*args):
                return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.PIPE)

            git("init", "--quiet")
            git("config", "core.autocrlf", "true")
            git("config", "core.safecrlf", "false")
            (root / ".gitattributes").write_bytes(attributes.read_bytes())
            original = b"original line one\r\noriginal line two\r\n"
            preserved = [
                "firmware/pico-c/oem/original.txt",
                "firmware/pico-s-pro/reverse-engineered/source.c",
                "firmware/pico-s-pro/modified/recovery/tests/source.py",
                "firmware/pico-s-pro/modified/firmware/standalone/report.json",
            ]
            for relative in [*preserved, "server/tools/active.py"]:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(original)
            git("add", ".")
            for relative in preserved:
                self.assertEqual(git("show", ":" + relative), original)
            self.assertEqual(git("show", ":server/tools/active.py"), original.replace(b"\r\n", b"\n"))
            for relative in [*preserved, "server/tools/active.py"]:
                (root / relative).unlink()
            git("checkout-index", "--all", "--force")
            for relative in preserved:
                self.assertEqual((root / relative).read_bytes(), original)
            self.assertEqual((root / "server/tools/active.py").read_bytes(), original.replace(b"\r\n", b"\n"))


if __name__ == "__main__":
    unittest.main()
