#!/usr/bin/env python3
"""Compile the Basic diagnostic owner and JSON adapter with the pinned ESP32 SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import acquire_arduinojson
import acquire_esp32

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['src/basic_console_tests.cpp', 'adapters/arduinojson/basic_console_tests_json.cpp']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--arduino', type=Path, required=True)
    parser.add_argument('--arduinojson', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--write-report', action='store_true')
    args = parser.parse_args()
    packages = {item['name']: item for item in json.loads(acquire_esp32.PIN.read_text())['packages']}
    acquire_esp32.verify_identity(args.toolchain, packages['xtensa-esp32-elf'])
    acquire_esp32.verify_identity(args.arduino, packages['arduino-esp32'])
    acquire_arduinojson.verify_source(args.arduinojson)
    compiler = args.toolchain / 'bin/xtensa-esp32-elf-g++'
    args.output.mkdir(parents=True, exist_ok=True)
    sdk = args.arduino / 'tools/sdk'
    includes = [ROOT / 'include', args.arduino / 'cores/esp32', args.arduino / 'variants/esp32', args.arduinojson / 'src']
    includes += sorted(path for path in (sdk / 'include').iterdir() if path.is_dir())
    common = ['-Os', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
              '-ffunction-sections', '-fdata-sections', '-fstrict-volatile-bitfields',
              '-mlongcalls', '-fno-fast-math', '-ffp-contract=off', '-DESP_PLATFORM',
              '-DESP32', '-DARDUINO_ARCH_ESP32', '-DARDUINO=10805',
              '-DF_CPU=240000000L', '-DCORE_DEBUG_LEVEL=0', '-DHAVE_CONFIG_H']
    cpp = ['-std=gnu++11', '-fno-exceptions', '-fno-rtti']
    evidence = []
    for source in SOURCES:
        output = args.output / (source.replace('/', '_') + '.o')
        subprocess.run([str(compiler)] + cpp + common + ['-I' + str(path) for path in includes] +
                       ['-c', str(ROOT / source), '-o', str(output)], check=True)
        evidence.append(dict(source=source, source_sha256=digest(ROOT / source),
                             object_sha256=digest(output), object_bytes=output.stat().st_size))
    headers = ['include/zseries/basic_console_tests.hpp', 'include/zseries/basic_console_json.hpp',
               'include/zseries/application_console.hpp', 'include/zseries/console.hpp',
               'include/zseries/backend_console_tests.hpp', 'include/zseries/console_test_support.hpp',
               'include/zseries/program_diagnostics.hpp', 'adapters/arduinojson/recipe_document.hpp']
    headers += [str(path.relative_to(ROOT)) for path in sorted((ROOT / 'src').glob('basic_console_*.inc'))]
    report = dict(schema_version=1, compiler=subprocess.check_output([str(compiler), '--version'], text=True).splitlines()[0],
                  compiler_sha256=digest(compiler), common_flags=common, cpp_flags=cpp, sources=evidence,
                  sdk_identity_sha256=packages['arduino-esp32']['identity_sha256'],
                  esp32_dependency_pin_sha256=digest(acquire_esp32.PIN),
                  arduinojson_dependency=json.loads(acquire_arduinojson.PIN.read_text()),
                  source_inputs={path: digest(ROOT / path) for path in headers},
                  build_script_sha256=digest(__file__), hardware_tested=False, complete_application_linked=False,
                  scope='Pinned GCC 5.2 compilation of the Basic diagnostic aggregate/leaves and selected ArduinoJson adapter. No application link, live network, peripheral transaction or hardware execution is claimed.')
    (args.output / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    if args.write_report:
        (ROOT / 'analysis/basic-console-target-build.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Compiled both Basic diagnostic target modules')


if __name__ == '__main__':
    main()
