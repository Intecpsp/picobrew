#!/usr/bin/env python3
"""Compile the Core orchestration and BinaryDownload diagnostic chunk with the pinned ESP32 compiler."""
from pathlib import Path
import argparse
import json
import subprocess
import acquire_arduinojson
import acquire_esp32
from build_esp32_objects import ROOT, digest, ESP32_PACKAGES, SDK_IDENTITIES

SOURCES = ['src/core_console_tests.cpp', 'src/binary_download_console_tests.cpp',
           'src/application_console.cpp']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['toolchain', 'arduino', 'arduinojson', 'output']:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--write-report', action='store_true')
    args = parser.parse_args()
    acquire_esp32.verify_identity(args.toolchain, ESP32_PACKAGES['xtensa-esp32-elf'])
    for path, expected in SDK_IDENTITIES.items():
        if digest(args.arduino / path) != expected:
            raise ValueError('Pinned SDK identity differs: '+path)
    acquire_arduinojson.verify_source(args.arduinojson)
    compiler = args.toolchain / 'bin/xtensa-esp32-elf-g++'
    args.output.mkdir(parents=True, exist_ok=True)
    flags = ['-std=gnu++11', '-Os', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
             '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections',
             '-fstrict-volatile-bitfields', '-mlongcalls', '-fno-fast-math', '-ffp-contract=off']
    sources, headers = [], set()
    for relative in SOURCES:
        source = ROOT / relative
        output = args.output / (relative.replace('/', '_')+'.o')
        dependency = output.with_suffix('.d')
        subprocess.run([str(compiler)]+flags+['-I'+str(ROOT / 'include'),
            '-I'+str(args.arduinojson / 'src'), '-MMD', '-MF', str(dependency),
            '-c', str(source), '-o', str(output)], check=True)
        sources.append(dict(source=relative, source_sha256=digest(source),
                            object_sha256=digest(output), object_bytes=output.stat().st_size))
        for token in dependency.read_text().replace('\\\n', ' ').split()[1:]:
            path = Path(token)
            if path.is_file() and path.is_relative_to(ROOT) and path.suffix in ['.hpp', '.h', '.inc']:
                headers.add(path.relative_to(ROOT).as_posix())
    report = dict(schema_version=1, compiler=subprocess.check_output([str(compiler), '--version'], text=True).splitlines()[0],
        compiler_sha256=digest(compiler), arduino_reference='espressif/arduino-esp32@9a7946e6859130146e56231c27a4aa177d9175a1 (1.0.1)',
        sdk_identity_sha256=SDK_IDENTITIES, flags=flags,
        arduinojson_dependency=json.loads(acquire_arduinojson.PIN.read_text()),
        sources=sources, owned_header_sha256={name: digest(ROOT / name) for name in sorted(headers)},
        build_script_sha256=digest(__file__), complete_application_linked=False, hardware_tested=False,
        scope='Pinned target object compilation of the Core Network/Int/All orchestrators, BinaryDownload diagnostic and application registry. No final application link or hardware execution.')
    (args.output / 'build-report.json').write_text(json.dumps(report, indent=2)+'\n')
    if args.write_report:
        (ROOT / 'analysis/network-console-target-build.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Compiled', len(sources), 'Core orchestration/BinaryDownload target objects')


if __name__ == '__main__':
    main()
