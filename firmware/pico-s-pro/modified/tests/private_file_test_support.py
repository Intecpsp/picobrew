"""Native permission fixtures for tests; never changes real user files."""

import os


def make_public(path, private_module):
    """Give Everyone read access on Windows, or mode 0644 on POSIX."""
    if os.name != "nt":
        path.chmod(0o644)
        return
    import ctypes as C
    from ctypes import wintypes as W
    native = private_module._helper
    set_security = native._declare(native._advapi, "SetNamedSecurityInfoW", W.DWORD,
        W.LPWSTR, C.c_int, W.DWORD, W.LPVOID, W.LPVOID, W.LPVOID, W.LPVOID)
    get_dacl = native._declare(native._advapi, "GetSecurityDescriptorDacl", W.BOOL,
        W.LPVOID, C.POINTER(W.BOOL), C.POINTER(W.LPVOID), C.POINTER(W.BOOL))
    descriptor, dacl = W.LPVOID(), W.LPVOID()
    present, defaulted = W.BOOL(), W.BOOL()
    user = native._current_sid()
    if not native._ConvertSD(f"D:P(A;;FA;;;{user})(A;;GR;;;WD)", 1, C.byref(descriptor), None):
        raise native._win_error(path)
    try:
        if not get_dacl(descriptor, C.byref(present), C.byref(dacl), C.byref(defaulted)):
            raise native._win_error(path)
        error = set_security(str(path), 1, 0x4 | 0x80000000, None, None, dacl, None)
        if error:
            raise C.WinError(error)
    finally:
        native._LocalFree(descriptor)
