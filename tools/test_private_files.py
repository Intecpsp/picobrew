#!/usr/bin/env python3
"""Exercise real native private-file permissions without USB or elevation."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import private_files as private


class PrivateFilesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_exclusive_private_creation_and_bounded_read(self):
        directory = self.root / "private" / "nested"
        private.ensure_private_directory(directory, parents=True)
        private.assert_private(directory, directory=True)
        private.assert_private(directory.parent, directory=True)
        path = directory / "api.key"
        with private.new_private_file(path) as handle:
            handle.write(b"synthetic key\n")
        private.assert_private(path)
        self.assertEqual(private.read_regular(path, 32, private=True), b"synthetic key\n")
        with self.assertRaises(FileExistsError):
            private.new_private_file(path)
        with self.assertRaises(private.PrivateFileError):
            private.read_regular(path, 3, private=True)
        with self.assertRaises(OSError):
            private.read_regular(directory, 32)

    def test_publication_retains_native_privacy_and_refuses_replacement(self):
        partial, target = self.root / "backup.partial", self.root / "backup.bin"
        with private.new_private_file(partial) as handle:
            handle.write(b"verified synthetic backup")
            private.publish(handle, partial, target)
            self.assertFalse(partial.exists())
        self.assertEqual(private.read_regular(target, 64, private=True), b"verified synthetic backup")
        with private.new_private_file(partial) as handle:
            handle.write(b"second")
            with self.assertRaises(OSError):
                private.publish(handle, partial, target)
        self.assertEqual(target.read_bytes(), b"verified synthetic backup")
        self.assertEqual(partial.read_bytes(), b"second")
        private.assert_private(target)
        private.assert_private(partial)

    def test_discard_removes_only_open_temporary_file(self):
        partial = self.root / "verification.partial"
        with private.new_private_file(partial) as handle:
            handle.write(b"second pass")
            private.discard(handle, partial)
            self.assertTrue(handle.closed)
            self.assertFalse(partial.exists())

    @unittest.skipUnless(os.name == "posix", "POSIX O_NOFOLLOW and FIFO behavior")
    def test_posix_read_rejects_symlink_fifo_and_public_key(self):
        path = self.root / "key"
        with private.new_private_file(path) as handle:
            handle.write(b"secret")
        link = self.root / "link"
        link.symlink_to(path)
        with self.assertRaises(OSError):
            private.read_regular(link, 32, private=True)
        fifo = self.root / "fifo"
        os.mkfifo(fifo, 0o600)
        with self.assertRaises(private.PrivateFileError):
            private.read_regular(fifo, 32)
        path.chmod(0o644)
        with self.assertRaises(private.PrivateFileError):
            private.read_regular(path, 32, private=True)

    @unittest.skipUnless(os.name == "posix", "POSIX pathname replacement")
    def test_posix_publish_rejects_replaced_partial(self):
        partial = self.root / "backup.partial"
        with private.new_private_file(partial) as handle:
            handle.write(b"verified")
            partial.rename(self.root / "moved")
            partial.write_bytes(b"not verified")
            with self.assertRaises(private.PrivateFileError):
                private.publish(handle, partial, self.root / "final")
        self.assertFalse((self.root / "final").exists())


@unittest.skipUnless(os.name == "nt", "Native Windows ACL and junction tests")
class WindowsPrivateFilesTests(PrivateFilesTests):
    def setUp(self):
        super().setUp()
        import ctypes as C
        from ctypes import wintypes as W
        self.C, self.W = C, W
        self.set_security = private._declare(private._advapi, "SetNamedSecurityInfoW", W.DWORD,
            W.LPWSTR, C.c_int, W.DWORD, W.LPVOID, W.LPVOID, W.LPVOID, W.LPVOID)
        self.get_owner = private._declare(private._advapi, "GetSecurityDescriptorOwner", W.BOOL,
            W.LPVOID, C.POINTER(W.LPVOID), C.POINTER(W.BOOL))
        self.get_dacl = private._declare(private._advapi, "GetSecurityDescriptorDacl", W.BOOL,
            W.LPVOID, C.POINTER(W.BOOL), C.POINTER(W.LPVOID), C.POINTER(W.BOOL))

    def set_dacl(self, path, sddl):
        C, W = self.C, self.W
        descriptor, dacl = W.LPVOID(), W.LPVOID()
        present, defaulted = W.BOOL(), W.BOOL()
        self.assertTrue(private._ConvertSD(sddl, 1, C.byref(descriptor), None))
        try:
            self.assertTrue(self.get_dacl(descriptor, C.byref(present), C.byref(dacl), C.byref(defaulted)))
            error = self.set_security(str(path), 1, 0x4 | 0x80000000, None, None, dacl, None)
            if error:
                raise C.WinError(error)
        finally:
            private._LocalFree(descriptor)

    def owner(self, path, directory=False):
        C, W = self.C, self.W
        handle = private._open_handle(path, private._READ_CONTROL, directory=directory, share=7)
        owner, descriptor = W.LPVOID(), W.LPVOID()
        try:
            error = private._GetSecurityInfo(handle, 1, 0x1, C.byref(owner), None,
                                             None, None, C.byref(descriptor))
            if error:
                raise C.WinError(error)
            return private._sid_text(owner)
        finally:
            if descriptor:
                private._LocalFree(descriptor)
            private._CloseHandle(handle)

    def test_new_files_have_current_user_owner_and_block_inherited_public_acl(self):
        user = private._current_sid()
        self.set_dacl(self.root, f"D:P(A;OICI;FA;;;{user})(A;OICI;GR;;;WD)")
        directory = self.root / "private"
        private.ensure_private_directory(directory)
        path = directory / "api.key"
        with private.new_private_file(path) as handle:
            handle.write(b"secret")
        self.assertEqual(self.owner(path), user)
        self.assertEqual(self.owner(directory, True), user)
        private.assert_private(path)
        private.assert_private(directory, directory=True)
        # SQLite creates its own journal/WAL; it must inherit protected access.
        sidecar = directory / "db.sqlite-wal"
        sidecar.write_bytes(b"synthetic WAL")
        private.assert_private(sidecar)

    def test_public_acl_is_rejected_without_rewriting_existing_permissions(self):
        user = private._current_sid()
        directory = self.root / "private"
        private.ensure_private_directory(directory)
        path = directory / "api.key"
        with private.new_private_file(path) as handle:
            handle.write(b"secret")
        self.set_dacl(path, f"D:P(A;;FA;;;{user})(A;;GR;;;WD)")
        with self.assertRaises(private.PrivateFileError):
            private.read_regular(path, 32, private=True)
        with self.assertRaises(private.PrivateFileError):
            private.ensure_private_file(path)
        self.set_dacl(directory, f"D:P(A;;FA;;;{user})(A;;GR;;;WD)")
        with self.assertRaises(private.PrivateFileError):
            private.ensure_private_directory(directory, exist_ok=True)
        # A second check must still fail: the helper did not 'repair' a chosen
        # existing directory by silently changing its ACL or its children's.
        with self.assertRaises(private.PrivateFileError):
            private.assert_private(directory, directory=True)
        with self.assertRaises(private.PrivateFileError):
            private.assert_private(path)

    def test_foreign_owner_and_null_dacl_are_rejected(self):
        C, W = self.C, self.W
        user = private._current_sid()
        for sddl in [f"O:WDD:P(A;;FA;;;{user})", f"O:{user}D:NO_ACCESS_CONTROL"]:
            with self.subTest(sddl=sddl):
                descriptor, owner, dacl = W.LPVOID(), W.LPVOID(), W.LPVOID()
                present, defaulted = W.BOOL(), W.BOOL()
                self.assertTrue(private._ConvertSD(sddl, 1, C.byref(descriptor), None))
                try:
                    self.assertTrue(self.get_owner(descriptor, C.byref(owner), C.byref(defaulted)))
                    self.assertTrue(self.get_dacl(descriptor, C.byref(present), C.byref(dacl), C.byref(defaulted)))
                    with self.assertRaises(private.PrivateFileError):
                        private._check_descriptor(owner, dacl)
                finally:
                    private._LocalFree(descriptor)

    def test_private_directory_must_protect_new_child_files(self):
        directory = self.root / "private"
        private.ensure_private_directory(directory)
        self.set_dacl(directory, f"D:P(A;;FA;;;{private._current_sid()})")
        with self.assertRaisesRegex(private.PrivateFileError, "newly created files"):
            private.ensure_private_directory(directory, exist_ok=True)

    def test_path_aliases_are_rejected_before_file_access(self):
        for component in ["data.", "data ", "CON", "nul.txt", "COM1", "LPT²", "CONOUT$", "a*b", "a?b", "file:stream", "key\x00ignored", "control\x1f"]:
            with self.subTest(component=component), self.assertRaises(private.PrivateFileError):
                private._normal_path(self.root / component / "api.key")

    def test_junctions_are_rejected_without_symlink_privilege(self):
        target, junction = self.root / "target", self.root / "junction"
        private.ensure_private_directory(target)
        result = subprocess.run(["cmd", "/d", "/c", "mklink", "/J", str(junction), str(target)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        try:
            with self.assertRaises(private.PrivateFileError):
                private.ensure_private_directory(junction, exist_ok=True)
            with self.assertRaises(private.PrivateFileError):
                private.new_private_file(junction / "key")
            with private.new_private_file(target / "key") as handle:
                handle.write(b"secret")
            with self.assertRaises(private.PrivateFileError):
                private.read_regular(junction / "key", 32, private=True)
        finally:
            junction.rmdir()

    def test_open_partial_cannot_be_swapped_before_publication(self):
        partial, replacement = self.root / "partial", self.root / "replacement"
        replacement.write_bytes(b"unverified")
        with private.new_private_file(partial) as handle:
            handle.write(b"verified")
            with self.assertRaises(OSError):
                os.replace(replacement, partial)
            private.publish(handle, partial, self.root / "final")
        self.assertEqual((self.root / "final").read_bytes(), b"verified")


if __name__ == "__main__":
    unittest.main(verbosity=2)
