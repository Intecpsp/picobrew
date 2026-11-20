#!/usr/bin/env python3
"""Build the reconstructed C core and an explicitly non-flashable test ELF."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm-prefix', default=os.environ.get('PICOBREW_ARM_PREFIX'))
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/core')
    parser.add_argument('--display', choices=('ssd1325','ssd1309'), default='ssd1325')
    parser.add_argument('--with-recipe', action='store_true',
                        help='Include the reconstructed recipe flows and their boundary fixture')
    parser.add_argument('--with-network', action='store_true',
                        help='Include the network/storage modules and explicit I/O fixture')
    parser.add_argument('--with-application', action='store_true',
                        help='Include all application sources and boot/service-binding fixtures')
    args = parser.parse_args()
    if args.with_application:
        args.with_recipe = args.with_network = True
    prefix = args.arm_prefix
    if not prefix:
        compiler = shutil.which('arm-none-eabi-gcc')
        if not compiler:
            parser.error('Set PICOBREW_ARM_PREFIX to the pinned arm-none-eabi- tool prefix')
        prefix = compiler[:-3]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    flags = ['-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft', '-mabi=aapcs',
             '--specs=nano.specs', '-std=gnu11', '-Os', '-g', '-ffunction-sections',
             '-fdata-sections', '-fno-fast-math', '-ffp-contract=off',
             '-fexcess-precision=standard', '-Wall', '-Wextra', '-Werror',
             '-I' + str(ROOT / 'include'),
             '-DPICO_C_DISPLAY_SSD1309=' + str(int(args.display == 'ssd1309'))]
    # The appliance integration is reconstructed separately. Do not silently
    # add work-in-progress UI/network modules to this function-level test link.
    sources = [ROOT / 'src' / name for name in (
        'actuators.c', 'encoder.c', 'network_parse.c', 'sensors.c', 'pid.c',
        'state.c', 'control.c', 'phase.c', 'device.c', 'rfid.c')]
    if args.with_recipe:
        sources += [ROOT / 'src' / name for name in (
            'recipe.c', 'recipe_programs.c', 'recipe_utilities.c', 'network/text.c')]
    if args.with_network:
        sources += [source for source in sorted((ROOT / 'src/network').glob('*.c'))
                    if source not in sources]
    if args.with_application:
        sources = sorted((ROOT / 'src').rglob('*.c'))
    objects = []
    for source in sources:
        target = output / 'objects' / source.relative_to(ROOT / 'src').with_suffix('.o')
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([prefix + 'gcc', *flags, '-c', str(source), '-o', str(target)], check=True)
        objects.append(target)
    archive = output / 'libpico_c.a'
    archive.unlink(missing_ok=True)
    subprocess.run([prefix + 'ar', 'rcs', str(archive), *map(str, objects)], check=True)
    fixtures = [ROOT / 'tests' / name for name in (
        'validation_entry.c', 'rfid_fixture.c', 'phase_fixture.c', 'control_fixture.c',
        'device_fixture.c')]
    if args.with_recipe:
        fixtures.append(ROOT / 'tests/recipe_fixture.c')
    if args.with_network:
        fixtures.append(ROOT / 'tests/network_fixture.c')
    if args.with_application:
        fixtures.append(ROOT / 'tests/application_fixture.c')
    test_objects = []
    for source in fixtures:
        target = output / ('test_' + source.stem + '.o')
        subprocess.run([prefix + 'gcc', *flags, '-c', str(source), '-o', str(target)], check=True)
        test_objects.append(target)
    elf = output / 'application-validation.elf'
    subprocess.run([prefix + 'gcc', '-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft',
                    '-mabi=aapcs', '-nostartfiles', '--specs=nano.specs', '--specs=nosys.specs',
                    '-T', str(ROOT / 'tests/validation.ld'),
                    '-Wl,-Map=' + str(elf.with_suffix('.map')), *map(str, test_objects),
                    '-Wl,--whole-archive', str(archive), '-Wl,--no-whole-archive',
                    '-Wl,--start-group', '-lm', '-lc', '-lgcc', '-Wl,--end-group',
                    '-o', str(elf)], check=True)
    undefined = subprocess.check_output([prefix + 'nm', '--undefined-only', str(elf)], text=True)
    if undefined.strip():
        raise ValueError('Unresolved validation symbols: ' + undefined)
    report = {'schema_version': 1, 'buildable_core': True, 'complete_firmware': False,
              'flashable': False, 'hardware_tested': False,
              'compiler': subprocess.check_output([prefix + 'gcc', '--version'], text=True).splitlines()[0],
              'source_sha256': {str(p.relative_to(ROOT)): digest(p) for p in sources},
              'header_sha256': {str(p.relative_to(ROOT)): digest(p) for p in sorted((ROOT / 'include').rglob('*.h'))},
              'fixture_sha256': {str(p.relative_to(ROOT)): digest(p) for p in fixtures},
              'archive_sha256': digest(archive), 'validation_elf_sha256': digest(elf),
              'limits': 'Function entry tests only. No appliance startup or physical peripherals are linked.'}
    (output / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'source_files': len(sources), 'complete_firmware': False,
                      'validation_elf': str(elf)}, indent=2))


if __name__ == '__main__':
    main()
