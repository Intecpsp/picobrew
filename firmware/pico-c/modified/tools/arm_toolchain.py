"""Locate an installed GNU Arm compiler without assuming Unix filenames."""

from pathlib import Path, PurePosixPath
import shutil


def sort_sources(paths):
    """Preserve the Linux build order on case-insensitive Windows paths too."""
    return sorted(paths, key=lambda path: PurePosixPath(path.as_posix()).parts)


def compiler_prefix(compiler):
    compiler = str(compiler)
    if compiler.lower().endswith('.exe'):
        compiler = compiler[:-4]
    if not compiler.lower().endswith('gcc'):
        raise ValueError('Expected the arm-none-eabi-gcc compiler executable')
    return compiler[:-3]


def find_arm_prefix(project):
    candidate = Path(project) / '.deps/toolchain/bin/arm-none-eabi-gcc'
    for executable in (candidate, candidate.with_name(candidate.name + '.exe')):
        if executable.is_file():
            return compiler_prefix(executable)
    compiler = shutil.which('arm-none-eabi-gcc')
    return compiler_prefix(compiler) if compiler else None
