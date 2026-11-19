#!/usr/bin/env python3
"""Compile and exercise standalone application workflows at SDK boundaries."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_host import compiler, executable_name, runtime_environment, sanitizer_flags
from arm_toolchain import sort_sources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    parser.add_argument('--no-sanitize', action='store_true')
    args = parser.parse_args()
    args.cc = args.cc or compiler()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='pico-control-test-') as temporary:
        sources = sort_sources((root / 'src').rglob('*.c'))
        environment = runtime_environment(compiler_paths=[args.cc])
        environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
        cases = [('control-tests', 'standalone_control_host.c', 1),
                 ('factory-reset-standalone', 'factory_reset_host.c', 1),
                 ('factory-reset-legacy', 'factory_reset_host.c', 0)]
        for name, harness, standalone in cases:
            executable = Path(temporary) / executable_name(name)
            command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
                       f'-DPICO_STANDALONE={standalone}', '-ffunction-sections', '-fdata-sections',
                       '-I' + str(root / 'include')]
            command += sanitizer_flags(not args.no_sanitize)
            inputs = list(sources)
            if not standalone:
                # Match the legacy application build: standalone-only modules
                # depend on local_control, which is disabled in this profile.
                inputs = [source for source in inputs
                          if not (source.name.startswith('local_') or source.name == 'http_server.c')]
            if harness == 'factory_reset_host.c':
                # Interpose the encoder input at compile time. macOS's linker
                # does not implement GNU ld --wrap. Keep the real encoder
                # module under a separate name, and route callers to the same
                # explicit input fixture used on the other hosts.
                encoder = root / 'src/ui/encoder.c'
                encoder_object = Path(temporary) / (name + '-encoder.o')
                subprocess.run([*command, '-Dpico_encoder_take_click=pico_test_real_encoder_take_click',
                                '-c', str(encoder), '-o', str(encoder_object)], check=True)
                inputs = [source for source in inputs if source != encoder]
                inputs.append(encoder_object)
                command.append('-Dpico_encoder_take_click=__wrap_pico_encoder_take_click')
            # Require complete linkage on every host, including linkers that
            # discard unused sections less aggressively than ELF ld.
            command += [*map(str, inputs), str(root / 'tests' / harness),
                        '-lm', '-o', str(executable)]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True, env=environment, timeout=30)


if __name__ == '__main__':
    main()
