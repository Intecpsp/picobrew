#!/usr/bin/env python3
"""Fetch pinned CC3200 source and an ARM compiler into a project-local directory.

No system packages are installed. Archives and guarded compatibility patches are
specified by dependencies/lock.json and dependencies/host-toolchains.json.
Host compiler packages support Linux x86-64, Windows x86/x86-64, and macOS
(the pinned Intel compiler requires Rosetta 2 on Apple Silicon).
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile


def compiler_for_host(lock, host_lock, system=None, machine=None):
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    if system == 'Linux' and machine in ('x86_64', 'amd64'):
        return lock['toolchain']
    if system == 'Windows' and machine in ('x86_64', 'amd64', 'x86', 'i386', 'i686'):
        return host_lock['toolchains']['windows-x86']
    if system == 'Darwin' and machine in ('x86_64', 'amd64', 'arm64', 'aarch64'):
        return host_lock['toolchains']['darwin-x86_64']
    raise RuntimeError(
        f'No pinned compiler for {system} {machine}; '
        'use --sdk-only with an independently installed ARM compiler'
    )


def extract_zip(archive, destination):
    """Extract regular Windows compiler files without traversal or symlinks."""
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            name = entry.filename
            relative = PurePosixPath(name)
            mode = entry.external_attr >> 16
            if (not relative.parts or relative.is_absolute()
                    or '\\' in name or '\0' in entry.orig_filename
                    or any(part in ('.', '..') or ':' in part for part in relative.parts)
                    or relative.as_posix() != name.rstrip('/')
                    or stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                raise RuntimeError('Unsafe compiler archive entry: ' + name)
            target = destination.joinpath(*relative.parts)
            if not target.resolve().is_relative_to(destination.resolve()):
                raise RuntimeError('Compiler archive entry escapes extraction directory: ' + name)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(entry) as input_file, target.open('xb') as output:
                shutil.copyfileobj(input_file, output, length=1024 * 1024)
            if os.name == 'posix' and mode & 0o777:
                target.chmod(mode & 0o777)


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as handle:
        while block := handle.read(1024 * 1024):
            checksum.update(block)
    return checksum.hexdigest()


def apply_patches(destination, patches):
    for patch in patches:
        path = destination / patch['file']
        actual = digest(path)
        if actual == patch['patched_sha256']:
            continue
        if actual != patch['original_sha256']:
            raise RuntimeError('Refusing to replace unexpected dependency content: ' + str(path))
        contents = path.read_bytes()
        before, after = patch['before'].encode(), patch['after'].encode()
        if contents.count(before) != 1:
            raise RuntimeError('Patch context is not unique: ' + str(path))
        patched = contents.replace(before, after)
        if hashlib.sha256(patched).hexdigest() != patch['patched_sha256']:
            raise RuntimeError('Patch output failed verification: ' + str(path))
        path.write_bytes(patched)


def pinned_archive(entry, prefix):
    archives = prefix / 'archives'
    archives.mkdir(parents=True, exist_ok=True)
    archive = archives / entry['archive_filename']
    if archive.exists() and (archive.stat().st_size != entry['bytes'] or digest(archive) != entry['sha256']):
        raise RuntimeError('Cached archive hash differs from the lock: ' + str(archive))
    if not archive.exists():
        partial = archive.with_name(archive.name + '.part')
        print('Downloading ' + entry['name'], flush=True)
        try:
            with urllib.request.urlopen(entry['url'], timeout=60) as response, partial.open('wb') as output:
                shutil.copyfileobj(response, output, length=1024 * 1024)
            if partial.stat().st_size != entry['bytes'] or digest(partial) != entry['sha256']:
                raise RuntimeError('Downloaded archive does not match its pinned size and SHA-256')
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    return archive


def install(entry, prefix):
    destination = prefix / entry['directory']
    marker = destination / '.picobrew-dependency.json'
    if destination.exists():
        if not marker.is_file():
            raise RuntimeError('Refusing to overwrite an existing untracked directory: ' + str(destination))
        previous = json.loads(marker.read_text(encoding='utf-8'))
        if previous.get('archive_sha256') != entry['sha256']:
            raise RuntimeError('Existing dependency has a different identity: ' + str(destination))
        apply_patches(destination, entry.get('patches', []))
        print('Using verified dependency identity: ' + str(destination), flush=True)
        return destination
    archive = pinned_archive(entry, prefix)
    with tempfile.TemporaryDirectory(prefix='extract-', dir=prefix) as temp:
        temporary = Path(temp)
        if archive.suffix.lower() == '.zip':
            extract_zip(archive, temporary)
        else:
            if not hasattr(tarfile, 'data_filter'):
                raise RuntimeError('Use Python 3.12+ or a Python version with tarfile.data_filter backported')
            with tarfile.open(archive) as tar:
                tar.extractall(temporary, filter='data')
        extracted = temporary / entry['archive_root']
        if not extracted.is_dir():
            raise RuntimeError('Expected archive root is absent: ' + entry['archive_root'])
        apply_patches(extracted, entry.get('patches', []))
        (extracted / '.picobrew-dependency.json').write_text(json.dumps({
            'name': entry['name'], 'archive_sha256': entry['sha256'],
            'url': entry['url'], 'patches': entry.get('patches', []),
        }, indent=2) + '\n', encoding='utf-8', newline='\n')
        extracted.rename(destination)
    print('Installed ' + str(destination), flush=True)
    return destination


def install_target_runtime(lock, host_lock, compiler_entry, prefix, compiler):
    """Use one hash-locked ARM runtime with each native host compiler."""
    runtime = host_lock['target_runtime']
    canonical = lock['toolchain']
    originals = compiler_entry.get('target_runtime_original_sha256', {})
    pending = []
    for item in runtime['files']:
        relative = PurePosixPath(item['path'])
        if (relative.is_absolute() or '..' in relative.parts
                or relative.as_posix() != item['path'] or relative.suffix != '.a'
                or not any(item['path'].startswith(base) for base in (
                    'arm-none-eabi/lib/' + runtime['multilib'] + '/',
                    'lib/gcc/arm-none-eabi/10.3.1/' + runtime['multilib'] + '/'))):
            raise RuntimeError('Invalid target runtime archive path: ' + item['path'])
        target = compiler / item['path']
        if target.is_symlink() or not target.is_file():
            raise RuntimeError('Missing or linked target runtime file: ' + str(target))
        actual = digest(target)
        if actual == item['sha256'] and target.stat().st_size == item['bytes']:
            continue
        if actual != originals.get(item['path']):
            raise RuntimeError('Refusing to replace unexpected target runtime content: ' + str(target))
        pending.append(item)
    if pending:
        archive = pinned_archive(canonical, prefix)
        expected = {canonical['archive_root'] + '/' + item['path']: item for item in pending}
        contents = {}
        with tarfile.open(archive) as source:
            for member in source:
                if member.name not in expected:
                    continue
                item = expected[member.name]
                # Arm's Linux tarball deduplicates identical multilib archives
                # with tar hard links. Resolve those inside the pinned archive;
                # never create filesystem links in the host compiler directory.
                if member.name in contents or not (member.isfile() or member.islnk()):
                    raise RuntimeError('Invalid canonical runtime archive member: ' + member.name)
                data = source.extractfile(member).read()
                if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
                    raise RuntimeError('Canonical runtime member hash differs from the host lock: ' + member.name)
                contents[member.name] = data
        if contents.keys() != expected.keys():
            raise RuntimeError('Canonical runtime archive is missing required ARM libraries')
        # Only verified ARM target archives are replaced. The native compiler,
        # linker, LTO plugin and all other host executables remain untouched.
        for name, data in contents.items():
            target = compiler / expected[name]['path']
            temporary = target.with_name(target.name + '.canonical-part')
            try:
                temporary.write_bytes(data)
                temporary.replace(target)
            finally:
                temporary.unlink(missing_ok=True)
        print('Installed canonical ' + runtime['multilib'] + ' ARM target runtime', flush=True)
    receipt = {
        'schema_version': 1,
        'multilib': runtime['multilib'],
        'host_compiler_archive_sha256': compiler_entry['sha256'],
        'target_runtime_source_archive_sha256': canonical['sha256'] if originals else compiler_entry['sha256'],
        'canonical_reference_archive_sha256': canonical['sha256'],
        'canonical_reference_url': canonical['url'],
        'reason': runtime['reason'],
        'files': runtime['files'],
    }
    (compiler / '.picobrew-target-runtime.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8', newline='\n')
    return receipt


def main():
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=project / '.deps')
    parser.add_argument('--lock', type=Path, default=project / 'dependencies/lock.json')
    parser.add_argument('--host-toolchains', type=Path, default=project / 'dependencies/host-toolchains.json')
    parser.add_argument('--sdk-only', action='store_true', help='Use an independently installed ARM compiler')
    args = parser.parse_args()
    lock = json.loads(args.lock.read_text(encoding='utf-8'))
    prefix = args.prefix.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    compiler_entry = None
    if not args.sdk_only:
        host_lock = json.loads(args.host_toolchains.read_text(encoding='utf-8'))
        compiler_entry = compiler_for_host(lock, host_lock)
        if note := compiler_entry.get('compatibility_note'):
            print(note, flush=True)
    sdk = install(lock['energia'], prefix)
    output = {'sdk': str(sdk)}
    if not args.sdk_only:
        compiler = install(compiler_entry, prefix)
        runtime = install_target_runtime(lock, host_lock, compiler_entry, prefix, compiler)
        cc = compiler / 'bin/arm-none-eabi-gcc'
        if not cc.is_file():
            cc = cc.with_name(cc.name + '.exe')
        output['arm_prefix'] = str(compiler / 'bin/arm-none-eabi-')
        output['compiler_archive_sha256'] = compiler_entry['sha256']
        output['compiler_host'] = compiler_entry['host']
        output['target_runtime'] = runtime
        try:
            output['compiler_version'] = subprocess.check_output([str(cc), '--version'], text=True).splitlines()[0]
        except (OSError, subprocess.CalledProcessError) as exc:
            hint = compiler_entry.get('compatibility_note', 'Install a compiler that can run on this host, or use --sdk-only.')
            raise RuntimeError('The pinned compiler could not run. ' + hint) from exc
    print(json.dumps(output, indent=2))


if __name__ == '__main__':
    raise SystemExit(main())
