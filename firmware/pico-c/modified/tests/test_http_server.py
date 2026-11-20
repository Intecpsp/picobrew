#!/usr/bin/env python3
"""Host-test the actual bounded HTTP state machine, including malformed input.

Uses address/undefined-behavior sanitizers on Linux/macOS unless --no-sanitize
is supplied. Windows executes the same assertions with native MinGW-w64.
No device, network service, or credentials are contacted.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_host import compiler, executable_name, runtime_environment, sanitizer_flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    parser.add_argument('--no-sanitize', action='store_true')
    args = parser.parse_args()
    args.cc = args.cc or compiler()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='pico-http-test-') as directory:
        command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
                   '-I' + str(root / 'include')]
        command += sanitizer_flags(not args.no_sanitize)
        command += [str(root / 'src/http_server.c')]
        # The firmware transport performs no heap allocation. LeakSanitizer is
        # unnecessary here and cannot run in ptrace-based build sandboxes;
        # address and undefined-behavior instrumentation remain enabled.
        environment = runtime_environment(compiler_paths=[args.cc])
        environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
        for fixture in ['http_server_fixture', 'http_asset_fixture']:
            executable = Path(directory) / executable_name(fixture)
            subprocess.run([*command, str(root / 'tests' / (fixture + '.c')),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True, env=environment)


if __name__ == '__main__':
    main()
