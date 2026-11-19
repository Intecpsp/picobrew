"""Private local files on Windows, macOS and Linux (standard library only).

Windows mode bits are not access controls. Files/directories are created with a
protected owner-only DACL; existing private inputs are checked through the same
handle used to read them. SYSTEM and local Administrators are trusted OS policy.
No privileges are enabled, existing ACLs are never rewritten, and unsupported
filesystems/ACLs fail closed. Reparse points, including parent junctions, are
rejected on Windows. POSIX keeps exclusive 0600/0700 creation and O_NOFOLLOW.

The Windows declarations follow CreateFileW, GetSecurityInfo, FILE_RENAME_INFO
and SECURITY_ATTRIBUTES in the Microsoft SDK. Native Windows tests exercise the
real APIs; emulated Unix mode bits are never used as a Windows security test.
"""

from contextlib import contextmanager
import os
from pathlib import Path
import stat


class PrivateFileError(OSError):
    """A local file cannot be used without weakening its privacy guarantees."""


if os.name == "nt":
    import ctypes as C
    from ctypes import wintypes as W
    import msvcrt

    _kernel = C.WinDLL("kernel32", use_last_error=True)
    _advapi = C.WinDLL("advapi32", use_last_error=True)
    _INVALID_HANDLE = C.c_void_p(-1).value
    _READ_CONTROL = 0x00020000
    _DELETE = 0x00010000
    _GENERIC_READ = 0x80000000
    _GENERIC_WRITE = 0x40000000
    _ATTR_DIRECTORY = 0x10
    _ATTR_REPARSE = 0x400
    _OPEN_REPARSE = 0x00200000
    _BACKUP_SEMANTICS = 0x02000000  # Directory handles; does not enable a privilege.

    class _SecurityAttributes(C.Structure):
        _fields_ = [("length", W.DWORD), ("descriptor", W.LPVOID), ("inherit", W.BOOL)]

    class _FileInfo(C.Structure):
        _fields_ = [("attributes", W.DWORD), ("created", W.FILETIME),
                    ("accessed", W.FILETIME), ("written", W.FILETIME),
                    ("volume", W.DWORD), ("size_high", W.DWORD), ("size_low", W.DWORD),
                    ("links", W.DWORD), ("index_high", W.DWORD), ("index_low", W.DWORD)]

    class _ACL(C.Structure):
        _fields_ = [("revision", W.BYTE), ("reserved", W.BYTE), ("size", W.WORD),
                    ("count", W.WORD), ("reserved2", W.WORD)]

    class _ACE(C.Structure):
        _fields_ = [("kind", W.BYTE), ("flags", W.BYTE), ("size", W.WORD),
                    ("mask", W.DWORD), ("sid_start", W.DWORD)]

    class _RenameInfo(C.Structure):
        # DWORD covers the SDK union { BOOLEAN ReplaceIfExists; DWORD Flags; }.
        # Native alignment puts HANDLE at offset 8 on x64 and 4 on x86.
        _fields_ = [("replace", W.DWORD), ("root", W.HANDLE),
                    ("name_bytes", W.DWORD), ("name", W.WCHAR * 1)]

    def _declare(dll, name, result, *args):
        function = getattr(dll, name)
        function.restype = result
        function.argtypes = args
        return function

    _CreateFile = _declare(_kernel, "CreateFileW", W.HANDLE, W.LPCWSTR, W.DWORD,
                          W.DWORD, C.POINTER(_SecurityAttributes), W.DWORD, W.DWORD, W.HANDLE)
    _CreateDirectory = _declare(_kernel, "CreateDirectoryW", W.BOOL, W.LPCWSTR,
                               C.POINTER(_SecurityAttributes))
    _CloseHandle = _declare(_kernel, "CloseHandle", W.BOOL, W.HANDLE)
    _GetCurrentProcess = _declare(_kernel, "GetCurrentProcess", W.HANDLE)
    _LocalFree = _declare(_kernel, "LocalFree", W.HANDLE, W.HANDLE)
    _GetFileInfo = _declare(_kernel, "GetFileInformationByHandle", W.BOOL,
                           W.HANDLE, C.POINTER(_FileInfo))
    _SetFileInfo = _declare(_kernel, "SetFileInformationByHandle", W.BOOL,
                           W.HANDLE, C.c_int, W.LPVOID, W.DWORD)
    _OpenProcessToken = _declare(_advapi, "OpenProcessToken", W.BOOL, W.HANDLE,
                                W.DWORD, C.POINTER(W.HANDLE))
    _GetTokenInformation = _declare(_advapi, "GetTokenInformation", W.BOOL,
                                   W.HANDLE, C.c_int, W.LPVOID, W.DWORD, C.POINTER(W.DWORD))
    _ConvertSid = _declare(_advapi, "ConvertSidToStringSidW", W.BOOL,
                          W.LPVOID, C.POINTER(W.LPWSTR))
    _ConvertSD = _declare(_advapi, "ConvertStringSecurityDescriptorToSecurityDescriptorW",
                         W.BOOL, W.LPCWSTR, W.DWORD, C.POINTER(W.LPVOID), C.POINTER(W.DWORD))
    _GetSecurityInfo = _declare(_advapi, "GetSecurityInfo", W.DWORD, W.HANDLE,
                               C.c_int, W.DWORD, C.POINTER(W.LPVOID), C.POINTER(W.LPVOID),
                               C.POINTER(W.LPVOID), C.POINTER(W.LPVOID), C.POINTER(W.LPVOID))
    _GetAce = _declare(_advapi, "GetAce", W.BOOL, W.LPVOID, W.DWORD, C.POINTER(W.LPVOID))

    def _win_error(path=None):
        error = C.WinError(C.get_last_error())
        if path is not None:
            error.filename = str(path)
        return error

    def _sid_text(sid):
        if not sid:
            raise PrivateFileError("Missing Windows owner/trustee SID")
        text = W.LPWSTR()
        if not _ConvertSid(sid, C.byref(text)):
            raise _win_error()
        try:
            return text.value
        finally:
            _LocalFree(C.cast(text, W.LPVOID))

    def _current_sid():
        token = W.HANDLE()
        if not _OpenProcessToken(_GetCurrentProcess(), 0x0008, C.byref(token)):
            raise _win_error()
        try:
            needed = W.DWORD()
            _GetTokenInformation(token, 1, None, 0, C.byref(needed))  # TokenUser
            if not needed.value:
                raise _win_error()
            data = C.create_string_buffer(needed.value)
            if not _GetTokenInformation(token, 1, data, len(data), C.byref(needed)):
                raise _win_error()
            return _sid_text(C.cast(data, C.POINTER(W.LPVOID))[0])
        finally:
            _CloseHandle(token)

    @contextmanager
    def _security_attributes(directory=False):
        user = _current_sid()
        inherit = "OICI" if directory else ""
        descriptor = W.LPVOID()
        if not _ConvertSD(f"O:{user}D:P(A;{inherit};FA;;;{user})", 1,
                          C.byref(descriptor), None):
            raise _win_error()
        try:
            yield _SecurityAttributes(C.sizeof(_SecurityAttributes), descriptor, False)
        finally:
            _LocalFree(descriptor)

    def _check_descriptor(owner, dacl, directory=False):
        trusted = {_current_sid(), "S-1-5-18", "S-1-5-32-544"}
        if _sid_text(owner) not in trusted:
            raise PrivateFileError("Private file/directory has another user's owner SID")
        if not dacl:
            raise PrivateFileError("Private file/directory has no restrictive Windows DACL")
        file_inheritance = False
        for index in range(C.cast(dacl, C.POINTER(_ACL)).contents.count):
            address = W.LPVOID()
            if not _GetAce(dacl, index, C.byref(address)):
                raise _win_error()
            ace = C.cast(address, C.POINTER(_ACE)).contents
            if ace.kind == 1:  # ACCESS_DENIED_ACE_TYPE cannot grant access.
                continue
            if ace.kind != 0:
                raise PrivateFileError("Unsupported Windows DACL entry; privacy cannot be verified")
            sid = _sid_text(address.value + _ACE.sid_start.offset)
            creator_owner = directory and sid == "S-1-3-0" and ace.flags & 0x08
            if ace.mask and sid not in trusted and not creator_owner:
                raise PrivateFileError(
                    "Private file/directory grants access to another Windows user/group; "
                    "restrict Security permissions to your account (SYSTEM and Administrators are allowed)")
            if ace.mask and ace.flags & 0x01:  # OBJECT_INHERIT_ACE
                file_inheritance = True
        if directory and not file_inheritance:
            raise PrivateFileError(
                "Private directory permissions must also apply to newly created files; "
                "choose a new private directory or apply private permissions to this folder and its files")

    def _check_acl(handle, directory=False):
        owner, dacl, descriptor = W.LPVOID(), W.LPVOID(), W.LPVOID()
        error = _GetSecurityInfo(handle, 1, 0x1 | 0x4, C.byref(owner), None,
                                 C.byref(dacl), None, C.byref(descriptor))
        if error:
            raise C.WinError(error)
        try:
            _check_descriptor(owner, dacl, directory)
        finally:
            _LocalFree(descriptor)

    def _check_handle(handle, directory=False, private=False):
        info = _FileInfo()
        if not _GetFileInfo(handle, C.byref(info)):
            raise _win_error()
        if info.attributes & _ATTR_REPARSE:
            raise PrivateFileError("Private paths cannot contain Windows reparse points or junctions")
        if bool(info.attributes & _ATTR_DIRECTORY) != directory:
            raise PrivateFileError("Expected a directory" if directory else "Expected a regular disk file")
        if private:
            _check_acl(handle, directory)
        return info

    def _normal_path(path):
        text = os.fspath(path)
        if any(ord(char) < 32 for char in text):
            raise PrivateFileError("Windows private paths cannot contain NUL or control characters")
        path = Path(os.path.abspath(text))
        text = str(path)
        if text.startswith(("\\\\?\\", "\\\\.\\")) or ":" in text[len(path.drive):]:
            raise PrivateFileError("Use a normal filesystem path without device prefixes or alternate streams")
        for part in path.parts[1:]:
            stem = part.split(".", 1)[0].upper()
            if part.endswith((" ", ".")) or any(c in part for c in '<>"|?*') or stem in {
                    "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"} or (
                    len(stem) == 4 and stem[:3] in {"COM", "LPT"} and stem[3] in "123456789¹²³"):
                raise PrivateFileError("Device names and trailing spaces/dots are not private-file paths")
        return path

    def _native(path):
        text = str(path)
        return "\\\\?\\UNC\\" + text[2:] if text.startswith("\\\\") else "\\\\?\\" + text

    def _open_handle(path, access, creation=3, attributes=None, directory=False, share=1):
        flags = _OPEN_REPARSE | (_BACKUP_SEMANTICS if directory else 0)
        handle = _CreateFile(_native(path), access, share,
                             C.byref(attributes) if attributes is not None else None,
                             creation, flags, None)
        if handle == _INVALID_HANDLE:
            raise _win_error(path)
        return handle

    @contextmanager
    def _directory_guard(path, *, create=False, private=False, exclusive=False):
        """Hold every ancestor against rename while resolving a Windows path."""
        path = _normal_path(path)
        handles = []
        try:
            for component in [*reversed(path.parents), path]:
                leaf = component == path
                access = 0x80 | (_READ_CONTROL if leaf and private else 0)
                try:
                    handle = _open_handle(component, access, directory=True, share=3)
                    if leaf and exclusive:
                        _CloseHandle(handle)
                        raise FileExistsError(str(path))
                except FileNotFoundError:
                    if not create:
                        raise
                    with _security_attributes(directory=True) as attributes:
                        if not _CreateDirectory(_native(component), C.byref(attributes)):
                            raise _win_error(component)
                    handle = _open_handle(component, access | _READ_CONTROL, directory=True, share=3)
                    try:
                        _check_handle(handle, directory=True, private=True)
                    except BaseException:
                        _CloseHandle(handle)
                        raise
                handles.append(handle)
                _check_handle(handle, directory=True, private=leaf and private)
            yield handles[-1]
        finally:
            for handle in reversed(handles):
                _CloseHandle(handle)

    def _file_object(handle, *, write=False):
        try:
            fd = msvcrt.open_osfhandle(handle, os.O_BINARY | os.O_NOINHERIT |
                                      (os.O_RDWR if write else os.O_RDONLY))
        except BaseException:
            _CloseHandle(handle)
            raise
        try:
            return os.fdopen(fd, "w+b" if write else "rb")
        except BaseException:
            os.close(fd)
            raise


def new_private_file(path, owner=None):
    """Exclusively create a binary read/write file, private before the first byte."""
    if os.name == "nt":
        if owner is not None:
            raise PrivateFileError("POSIX uid/gid ownership is not valid on Windows")
        path = _normal_path(path)
        with _directory_guard(path.parent), _security_attributes() as attributes:
            handle = _open_handle(path, _GENERIC_READ | _GENERIC_WRITE | _READ_CONTROL | _DELETE,
                                  creation=1, attributes=attributes)  # CREATE_NEW
            try:
                _check_handle(handle, private=True)
            except BaseException:
                _CloseHandle(handle)
                raise
        return _file_object(handle, write=True)
    flags = os.O_CREAT | os.O_EXCL | os.O_RDWR | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    try:
        if owner is not None:
            os.fchown(fd, *owner)
        return os.fdopen(fd, "w+b")
    except BaseException:
        os.close(fd)
        raise


def _open_regular(path, *, private=False, owner=None):
    if os.name == "nt":
        if owner is not None:
            raise PrivateFileError("POSIX uid/gid ownership is not valid on Windows")
        path = _normal_path(path)
        with _directory_guard(path.parent):
            handle = _open_handle(path, _GENERIC_READ | _READ_CONTROL)
            try:
                _check_handle(handle, private=private)
            except BaseException:
                _CloseHandle(handle)
                raise
        return _file_object(handle)
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise PrivateFileError("Local input is not a regular file")
        if private:
            expected = owner[0] if owner is not None else os.geteuid()
            if stat.S_IMODE(info.st_mode) & 0o077 or info.st_uid != expected:
                raise PrivateFileError("Private file must be owned by the invoking user with mode 0600")
        return os.fdopen(fd, "rb")
    except BaseException:
        os.close(fd)
        raise


def read_regular(path, limit, *, private=False, owner=None):
    """Read a bounded regular file through one checked, non-following handle."""
    with _open_regular(path, private=private, owner=owner) as handle:
        if os.fstat(handle.fileno()).st_size > limit:
            raise PrivateFileError("Local input exceeds the permitted size")
        data = handle.read(limit + 1)
        if len(data) > limit:
            raise PrivateFileError("Local input grew beyond the permitted size")
        return data


def assert_private(path, *, directory=False, owner=None):
    """Check native protection; useful to callers and cross-platform tests."""
    if os.name == "nt":
        if directory:
            with _directory_guard(path, private=True):
                return
        with _open_regular(path, private=True, owner=owner):
            return
    info = os.lstat(path)
    expected = owner[0] if owner is not None else os.geteuid()
    kind = stat.S_ISDIR if directory else stat.S_ISREG
    if not kind(info.st_mode) or info.st_uid != expected or stat.S_IMODE(info.st_mode) & 0o077:
        raise PrivateFileError("Private path requires invoking-user ownership and mode 0700/0600")


def ensure_private_directory(path, *, parents=False, exist_ok=False):
    """Create with private inherited access; never rewrite an existing ACL."""
    path = Path(path)
    if os.name == "nt":
        path = _normal_path(path)
        if not parents:
            with _directory_guard(path.parent):
                with _directory_guard(path, create=True, private=True, exclusive=not exist_ok):
                    return
        else:
            with _directory_guard(path, create=True, private=True, exclusive=not exist_ok):
                return
    # mkdir(parents=True) creates intermediate parents with the default mode;
    # create each missing component explicitly so all new parents are private.
    if parents and not path.parent.exists():
        ensure_private_directory(path.parent, parents=True, exist_ok=True)
    path.mkdir(mode=0o700, exist_ok=exist_ok)


def ensure_private_file(path):
    """Precreate/validate a SQLite file before the SQLite library opens it."""
    try:
        with new_private_file(path):
            return
    except FileExistsError:
        if os.name == "nt":
            assert_private(path)
            return
    # Preserve the telemetry receiver's existing POSIX tightening behavior.
    with _open_regular(path) as handle:
        os.fchmod(handle.fileno(), 0o600)


def publish(file, partial, destination):
    """Publish the checked open file without replacing any existing destination."""
    file.flush()
    os.fsync(file.fileno())
    if os.name == "nt":
        destination = _normal_path(destination)
        handle = msvcrt.get_osfhandle(file.fileno())
        _check_handle(handle, private=True)
        with _directory_guard(destination.parent):
            name = str(destination).encode("utf-16-le")
            size = max(C.sizeof(_RenameInfo), _RenameInfo.name.offset + len(name) + 2)
            buffer = C.create_string_buffer(size)
            info = _RenameInfo.from_buffer(buffer)
            info.replace = 0
            info.name_bytes = len(name)
            C.memmove(C.addressof(buffer) + _RenameInfo.name.offset, name, len(name))
            if not _SetFileInfo(handle, 3, buffer, size):  # FileRenameInfo
                raise _win_error(destination)
        return
    before = os.lstat(partial)
    if not stat.S_ISREG(before.st_mode) or not os.path.samestat(before, os.fstat(file.fileno())):
        raise PrivateFileError("Partial file no longer names the verified open file")
    os.link(partial, destination, follow_symlinks=False)
    os.unlink(partial)


def discard(file, path):
    """Remove a temporary file by its open handle on Windows, then close it."""
    if os.name == "nt":
        handle = msvcrt.get_osfhandle(file.fileno())
        _check_handle(handle, private=True)
        delete = C.c_ubyte(True)  # FILE_DISPOSITION_INFO.DeleteFile is BOOLEAN.
        if not _SetFileInfo(handle, 4, C.byref(delete), C.sizeof(delete)):  # FileDispositionInfo
            raise _win_error(path)
        file.close()
        return
    if not os.path.samestat(os.lstat(path), os.fstat(file.fileno())):
        raise PrivateFileError("Temporary path no longer names the open file")
    os.unlink(path)
    file.close()
