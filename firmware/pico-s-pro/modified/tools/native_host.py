"""Native GCC/Clang fixture support for Linux, macOS and Windows (MinGW-w64).

These helpers compile and load host simulations, never firmware for a device.
CC and CXX may name an executable or its full path, including spaces.
"""
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import platform
import shutil


def host_system():
    return platform.system()


def compiler(cxx=False):
    key = 'CXX' if cxx else 'CC'
    default = ('g++' if cxx else 'gcc') if host_system() == 'Windows' else ('c++' if cxx else 'cc')
    executable = os.environ.get(key, default)
    found = shutil.which(executable)
    if not found:
        raise RuntimeError(f'Native {key} compiler missing: {executable}. Install GCC/Clang (MinGW-w64 on Windows), or set {key} to the compiler executable.')
    return str(Path(found).resolve())


def executable_name(stem):
    return stem + ('.exe' if host_system() == 'Windows' else '')


def library_name(stem):
    return stem + {'Windows': '.dll', 'Darwin': '.dylib'}.get(host_system(), '.so')


def pic_flags():
    return [] if host_system() == 'Windows' else ['-fPIC']


def shared_flags():
    if host_system() == 'Darwin':
        return ['-dynamiclib']
    if host_system() == 'Windows':
        return ['-shared', '-Wl,--export-all-symbols']
    return ['-shared']


def gc_sections_flags():
    return ['-Wl,-dead_strip'] if host_system() == 'Darwin' else ['-Wl,--gc-sections']


def sanitizer_flags(enabled=True):
    # MinGW-w64 does not ship the GCC ASan/UBSan runtimes. Its tests execute the
    # same fixtures and assertions; Linux/macOS additionally instrument them.
    if not enabled or host_system() == 'Windows':
        return []
    return ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']


def _compiler_directories():
    directories = []
    for cxx in (False, True):
        try:
            directory = str(Path(compiler(cxx)).parent)
        except RuntimeError:
            continue
        if directory not in directories:
            directories.append(directory)
    return directories


def runtime_environment(environment=None, *, compiler_paths=()):
    result = dict(os.environ if environment is None else environment)
    if host_system() == 'Windows':
        # A compiler selected by an absolute CC/CXX path need not be on PATH.
        # Child executables still need its MinGW runtime DLLs.
        explicit = [str(Path(path).resolve().parent) for path in compiler_paths]
        result['PATH'] = os.pathsep.join([*explicit, *_compiler_directories(), result.get('PATH', '')])
    return result


def load_library(path):
    directories = []
    if host_system() == 'Windows':
        # Python 3.8+ resolves ctypes dependencies using explicit DLL search
        # directories, not the process's PATH. Keep them alive with the library.
        directories = [os.add_dll_directory(directory) for directory in _compiler_directories()]
    try:
        library = ctypes.CDLL(str(path))
    except BaseException:
        for directory in directories:
            directory.close()
        raise
    library._native_host_directories = directories
    return library


def close_library(library):
    if host_system() == 'Windows' and library._handle:
        # Windows holds an open DLL file until it is unloaded. Tests call this
        # only after their last C function call, before deleting the temp tree.
        from _ctypes import FreeLibrary
        FreeLibrary(library._handle)
        library._handle = 0
    for directory in library._native_host_directories:
        directory.close()
    library._native_host_directories = []


@contextmanager
def native_library(path):
    library = load_library(path)
    try:
        yield library
    finally:
        close_library(library)
