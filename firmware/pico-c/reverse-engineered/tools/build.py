#!/usr/bin/env python3
"""Compile recovered application modules for Cortex-M4 with the real ARM ABI.

The archive contains only the recovered application. This entry creates archives only; build_firmware.py supplies the real
SDK/startup link. Function-level test fixtures use build_core.py instead.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--display', choices=['ssd1325','ssd1309'], default='ssd1325')
    parser.add_argument('--arm-prefix', default=os.environ.get('PICOBREW_ARM_PREFIX'))
    parser.add_argument('--sdk-root', type=Path,
                        default=Path(os.environ.get('PICOBREW_SDK', str(ROOT / '.deps/energia'))))
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--archive-only', action='store_true')
    parser.add_argument('--core-only', action='store_true',
                        help='Build portable application core without the Energia adapter')
    options = parser.parse_args()
    prefix = options.arm_prefix
    if not prefix:
        candidate = ROOT / '.deps/toolchain/bin/arm-none-eabi-gcc'
        compiler = str(candidate) if candidate.is_file() else shutil.which('arm-none-eabi-gcc')
        if not compiler:
            parser.error('ARM compiler missing. Run the dependency bootstrap or set PICOBREW_ARM_PREFIX to the arm-none-eabi- tool prefix.')
        prefix = compiler[:-3]
    gcc, ar, nm, readelf = (prefix + suffix for suffix in ['gcc', 'ar', 'nm', 'readelf'])
    version = subprocess.check_output([gcc, '--version'], text=True).splitlines()[0]
    output = options.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    flags = ['-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft', '-mabi=aapcs',
             '--specs=nano.specs', '-std=gnu11', '-Os', '-g', '-ffunction-sections', '-fdata-sections',
             '-fno-fast-math', '-ffp-contract=off', '-fexcess-precision=standard',
             '-Wall', '-Wextra', '-Werror', '-DPICO_C_DISPLAY_SSD1309=' + str(int(options.display == 'ssd1309')), '-I' + str(ROOT / 'include')]
    sources = sorted((ROOT / 'src').rglob('*.c'))
    objects = []
    source_hashes = {}
    for source in sources:
        object_path = output / 'objects' / source.relative_to(ROOT / 'src').with_suffix('.o')
        object_path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([gcc, *flags, '-c', str(source), '-o', str(object_path)], check=True)
        objects.append(object_path)
        source_hashes[str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
    archive = output / 'libpicobrew.a'
    # Recreate it so deleted modules cannot linger from an earlier build.
    archive.unlink(missing_ok=True)
    subprocess.run([ar, 'rcs', str(archive), *(str(p) for p in objects)], check=True)
    report = {'application_archive': archive.name, 'source_files': len(sources),
              'display_variant': options.display,
              'compiler': version, 'flags': flags[:-1] + ['-Iinclude'],
              'source_sha256': source_hashes,
              'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
              'full_firmware': False, 'validation_elf': None}
    report['header_sha256'] = {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted((ROOT / 'include').rglob('*.h'))}
    if not options.core_only:
        sdk = options.sdk_root.resolve()
        if not (sdk / 'cores/cc3200/Energia.h').is_file():
            parser.error('Pinned SDK missing. Run make bootstrap or set PICOBREW_SDK. Use --core-only only for the portable application archive.')
        cxx = prefix + 'g++'
        cpp_flags = ['-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft', '-mabi=aapcs',
                     '--specs=nano.specs', '-std=gnu++11', '-Os', '-g', '-ffunction-sections', '-fdata-sections',
                     '-fno-rtti', '-fno-exceptions', '-fno-threadsafe-statics',
                     '-Wall', '-Wextra', '-Werror', '-DENERGIA', '-D__CC3200R1M1RGC__',
                     '-DPICO_C_DISPLAY_SSD1309=' + str(int(options.display == 'ssd1309')),
                     '-I' + str(ROOT / 'include')]
        for directory in ['cores/cc3200', 'variants/CC3200-LAUNCHXL', 'system',
                          'libraries/WiFi', 'libraries/SPI']:
            cpp_flags += ['-isystem', str(sdk / directory)]
        adapter_source = ROOT / 'platform/energia_cc3200.cpp'
        adapter_sources = sorted(p for p in (ROOT / 'platform').glob('*.cpp')
                                 if p.name != 'firmware_main.cpp')
        adapter_objects = []
        for source in adapter_sources:
            adapter_object = output / ('platform_' + source.stem + '.o')
            subprocess.run([cxx, *cpp_flags, '-c', str(source), '-o', str(adapter_object)], check=True)
            adapter_objects.append(adapter_object)
        adapter_archive = output / 'libpicobrew_energia.a'
        adapter_archive.unlink(missing_ok=True)
        subprocess.run([ar, 'rcs', str(adapter_archive), *map(str, adapter_objects)], check=True)
        report.update({'energia_adapter_archive': adapter_archive.name,
                       'energia_adapter_source_sha256': hashlib.sha256(adapter_source.read_bytes()).hexdigest(),
                       'platform_source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in adapter_sources},
                       'energia_adapter_sha256': hashlib.sha256(adapter_archive.read_bytes()).hexdigest(),
                       'sdk_dependency_lock_sha256': hashlib.sha256((ROOT / 'dependencies/lock.json').read_bytes()).hexdigest(),
                       'sdk_interface_check': 'Real pinned Energia/TI headers; C++11 Cortex-M4 soft-float compilation with warnings as errors'})
    (output / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ['source_files', 'compiler', 'application_archive',
                                                  'validation_elf', 'full_firmware']}, indent=2))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
