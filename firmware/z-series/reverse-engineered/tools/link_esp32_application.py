#!/usr/bin/env python3
"""Link verified reconstructed objects with the pinned historical ESP32 SDK.

The default entry is a non-installable link probe. --entry selects the concrete
application source once its SDK/Head bindings are complete. This command creates
an ELF and map only; it never chooses or writes a device's partition layout.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import acquire_esp32

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[2]
PIN = json.loads(acquire_esp32.PIN.read_text())
PACKAGES = {package['name']: package for package in PIN['packages']}
# These are the compiler's actual soft-double dependencies of the 1s ISR.
# Keep the same arithmetic implementation and move its input sections to IRAM;
# do not silently substitute a different ROM math implementation.
ISR_LIBGCC_OBJECTS = ['_addsubdf3.o', '_muldf3.o', '_cmpdf2.o', '_fixdfsi.o',
                      '_divdi3.o', '_udivdi3.o', '_umoddi3.o']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_objects(directory):
    report = json.loads((directory / 'build-report.json').read_text())
    for item in report['sources']:
        source = ROOT / item['source']
        obj = directory / (item['source'].replace('/', '_') + '.o')
        if digest(source) != item['source_sha256'] or digest(obj) != item['object_sha256']:
            raise ValueError('Stale source/object: ' + item['source'])
    for field in ['owned_header_sha256', 'platform_header_sha256', 'adapter_header_sha256', 'implementation_include_sha256']:
        if field not in report:
            raise ValueError('Rebuild reconstructed target objects to record all header inputs')
        for name, expected in report[field].items():
            if digest(ROOT / name) != expected:
                raise ValueError('Stale compiled header: ' + name)
    if not report.get('build_input_sha256'):
        raise ValueError('Rebuild reconstructed target objects to record generator/dependency inputs')
    for name, expected in report['build_input_sha256'].items():
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or digest(REPOSITORY / path) != expected:
            raise ValueError('Stale reconstructed build input: ' + name)
    if digest(directory / 'libzseries_recovered_esp32.a') != report['archive_sha256']:
        raise ValueError('Reconstructed archive changed')
    for name, expected in report['ethernet2_objects'].items():
        if digest(directory / name) != expected:
            raise ValueError('Ethernet object changed: ' + name)
    constants = report['original_backend_constants']
    if digest(directory / 'generated/oem_credentials.hpp') != constants['generated_header_sha256']:
        raise ValueError('Generated original backend constants changed')
    if digest(ROOT / 'tools/recover_backend_credentials.py') != constants['generator_sha256']:
        raise ValueError('Original backend constant generator changed')
    return report


def reconstructed_provenance(report):
    """Make the archive's verified provenance self-contained for packaging."""
    prefix = ROOT.relative_to(REPOSITORY)
    sources = [dict(source=(prefix / item['source']).as_posix(), source_sha256=item['source_sha256'])
               for item in report['sources']]
    headers = {}
    for field in ['owned_header_sha256', 'platform_header_sha256', 'adapter_header_sha256', 'implementation_include_sha256']:
        headers.update({(prefix / name).as_posix(): value for name, value in report[field].items()})
    headers.update(report['build_input_sha256'])
    headers[(prefix / 'tools/link_esp32_application.py').as_posix()] = digest(__file__)
    return sources, headers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--arduino', type=Path, required=True)
    parser.add_argument('--objects', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--entry', type=Path, default=ROOT / 'tests/target_link_probe.cpp')
    parser.add_argument('--profile', choices=['oem', 'standalone'], default='oem')
    parser.add_argument('--extra-source', type=Path, action='append', default=[])
    parser.add_argument('--extra-include', type=Path, action='append', default=[])
    args = parser.parse_args()
    toolchain, arduino = args.toolchain.resolve(), args.arduino.resolve()
    objects, output = args.objects.resolve(), args.output.resolve()
    acquire_esp32.verify_identity(toolchain, PACKAGES['xtensa-esp32-elf'])
    acquire_esp32.verify_identity(arduino, PACKAGES['arduino-esp32'])
    object_report = verify_objects(objects)
    reconstructed_sources, reconstructed_headers = reconstructed_provenance(object_report)
    output.mkdir(parents=True, exist_ok=True)
    sdk = arduino / 'tools/sdk'
    binary = toolchain / 'bin'
    cc, cxx = binary / 'xtensa-esp32-elf-gcc', binary / 'xtensa-esp32-elf-g++'
    ar = binary / 'xtensa-esp32-elf-ar'
    libraries = ['Wire', 'SPI', 'WiFi', 'WiFiClientSecure', 'Update', 'EEPROM', 'FS']
    includes = [objects / 'generated', ROOT / 'include', ROOT / 'platform/esp32', arduino / 'cores/esp32', arduino / 'variants/esp32',
                objects / 'ethernet2-adapted/src']
    sources = [path for path in sorted((arduino / 'cores/esp32').iterdir())
               if path.suffix in ['.c', '.cpp', '.S'] and path.name != 'main.cpp']
    for library in libraries:
        parent = arduino / 'libraries' / library
        folder = parent / 'src' if (parent / 'src').is_dir() else parent
        includes.append(folder)
        sources.extend(path for path in sorted(folder.rglob('*')) if path.suffix in ['.c', '.cpp', '.S'])
    includes.extend(sorted(path for path in (sdk / 'include').iterdir() if path.is_dir()))
    includes.extend(path.resolve() for path in args.extra_include)
    flags = object_report['common_flags']
    # The SDK is compiled with its upstream warning policy, while owned code
    # keeps -Werror. Reuse hashes to avoid rebuilding an unchanged SDK repeatedly.
    sdk_flags = [flag for flag in flags if flag != '-Werror'] + ['-w']
    cpp_flags = object_report['cpp_flags']
    include_flags = ['-I' + str(path) for path in includes]
    sdk_objects, evidence = [], []
    for source in sources:
        relative = source.relative_to(arduino).as_posix()
        obj = output / ('sdk_' + relative.replace('/', '_') + '.o')
        signature = output / (obj.name + '.json')
        program = cxx if source.suffix == '.cpp' else cc
        language = cpp_flags if source.suffix == '.cpp' else ['-std=gnu99'] if source.suffix == '.c' else ['-x', 'assembler-with-cpp']
        identity = dict(source_sha256=digest(source), flags=sdk_flags + language + include_flags,
                        compiler_sha256=digest(program))
        previous = json.loads(signature.read_text()) if signature.exists() else {}
        if not obj.exists() or previous.get('identity') != identity or previous.get('object_sha256') != digest(obj):
            subprocess.run([str(program)] + language + sdk_flags + include_flags + ['-c', str(source), '-o', str(obj)], check=True)
            signature.write_text(json.dumps(dict(identity=identity, object_sha256=digest(obj))) + '\n')
        sdk_objects.append(obj)
        evidence.append(dict(source=relative, source_sha256=digest(source), object_sha256=digest(obj)))
    sdk_archive = output / 'libarduino_required.a'
    if sdk_archive.exists():
        sdk_archive.unlink()
    subprocess.run([str(ar), 'rcs', str(sdk_archive)] + [str(path) for path in sdk_objects], check=True)
    entry_obj = output / 'application-entry.o'
    owned_flags = flags + ['-fstack-usage']
    subprocess.run([str(cxx)] + cpp_flags + owned_flags + include_flags + ['-c', str(args.entry), '-o', str(entry_obj)], check=True)
    extra_objects, extra_evidence = [], []
    for source in args.extra_source:
        source = source.resolve()
        relative = source.relative_to(REPOSITORY).as_posix()
        obj = output / ('owned_' + relative.replace('/', '_') + '.o')
        program = cxx if source.suffix == '.cpp' else cc
        language = cpp_flags if source.suffix == '.cpp' else ['-std=gnu99']
        subprocess.run([str(program)] + language + owned_flags + include_flags + ['-c', str(source), '-o', str(obj)], check=True)
        extra_objects.append(str(obj))
        extra_evidence.append(dict(source=relative, source_sha256=digest(source), object_sha256=digest(obj)))
    extra_headers = {}
    for folder in args.extra_include:
        for header in sorted(folder.resolve().rglob('*')):
            if header.suffix in ['.h', '.hpp', '.inc']:
                extra_headers[header.relative_to(REPOSITORY).as_posix()] = digest(header)
    # Use a checked copy of the SDK linker script. Earlier IRAM placement
    # consumes these sections before the later .flash.text wildcard sees them.
    original_ld = sdk / 'ld/esp32.common.ld'
    ld_text = original_ld.read_text()
    marker = '    *( .iram1  .iram1.*)'
    if ld_text.count(marker) != 1:
        raise ValueError('Pinned linker script IRAM placement changed')
    insertion = '\n'.join('    *libgcc.a:' + name + '( .literal .literal.* .text .text.*)' for name in ISR_LIBGCC_OBJECTS)
    owned_ld = output / 'esp32.application.ld'
    owned_ld.write_text(ld_text.replace(marker, marker + '\n' + insertion))
    properties = dict(line.split('=', 1) for line in (arduino / 'platform.txt').read_text().splitlines()
                      if line and not line.startswith('#') and '=' in line)
    sdk_libraries = properties['compiler.c.elf.libs'].split()
    elf = output / ('link-probe.elf' if args.entry.resolve() == (ROOT / 'tests/target_link_probe.cpp').resolve() else 'application.elf')
    map_file = elf.with_suffix('.map')
    linker = ['-nostdlib', '-L' + str(sdk / 'lib'), '-L' + str(sdk / 'ld'),
              '-T', 'esp32_out.ld', '-T', str(owned_ld), '-T', 'esp32.rom.ld', '-T', 'esp32.peripherals.ld',
              '-T', 'esp32.rom.spiram_incompatible_fns.ld', '-u', 'ld_include_panic_highint_hdl',
              '-u', 'call_user_start_cpu0', '-Wl,--gc-sections', '-Wl,-static', '-Wl,--undefined=uxTopUsedPriority',
              '-u', '__cxa_guard_dummy', '-u', '__cxx_fatal_exception', '-Wl,-Map=' + str(map_file), '-Wl,--cref',
              '-Wl,--start-group', str(entry_obj)] + extra_objects + [str(objects / 'libzseries_recovered_esp32.a'),
              str(objects / 'libethernet2_adapted.a'), str(sdk_archive)] + sdk_libraries + ['-Wl,--end-group', '-Wl,-EL', '-o', str(elf)]
    subprocess.run([str(cc)] + linker, check=True)
    symbols = subprocess.check_output([str(binary / 'xtensa-esp32-elf-nm'), '-n', str(elf)], text=True)
    addresses = {}
    for line in symbols.splitlines():
        match = re.match(r'^([0-9a-fA-F]+)\s+\w\s+(\S+)$', line)
        if match:
            addresses[match[2]] = int(match[1], 16)
    critical = []
    for fragment in ['heater_tick_20ms', 'heater_tick_1s', 'pid_compute']:
        matches=[name for name in addresses if fragment in name]
        if len(matches)!=1:raise ValueError('Missing/ambiguous heater dependency: '+fragment)
        critical.extend(matches)
    for name in ['__adddf3', '__subdf3', '__muldf3', '__gtdf2', '__ltdf2', '__fixdfsi']:
        if name not in addresses:raise ValueError('Missing compiler arithmetic dependency: '+name)
        critical.append(name)
    application=elf.name!='link-probe.elf'
    required={}
    if application:
        demangled={}
        for line in subprocess.check_output([str(binary/'xtensa-esp32-elf-nm'),'-n','-C',str(elf)],text=True).splitlines():
            match=re.match(r'^([0-9a-fA-F]+)\s+\w\s+(.+)$',line)
            if match:demangled[match[2]]=int(match[1],16)
        names=['app_main',('zseries::standalone::esp32::start_firmware()' if args.profile=='standalone' else 'zseries::oem::esp32::start_firmware()'),
               'zseries::oem::esp32::FirmwareApplication::setup()',
               'zseries::oem::esp32::FirmwareApplication::tick(unsigned int)',
               'zseries::oem::CoreApplication::setup()', 'zseries::oem::CoreApplication::tick(unsigned int)',
               'zseries::oem::HeadApplication::setup()', 'zseries::oem::HeadApplication::tick(unsigned int)']
        iram=['zseries::oem::esp32::FirmwareApplication::heater_second()',
              'zseries::oem::esp32::FirmwareApplication::heater_fast()',
              'zseries::oem::esp32::(anonymous namespace)::write_pin(void*, unsigned char, bool)',
              'zseries::oem::esp32::(anonymous namespace)::encoder_change()',
              'zseries::oem::esp32::(anonymous namespace)::button_falling()',
              'zseries::oem::esp32::(anonymous namespace)::read_pin(void*, unsigned char, int&)',
              'zseries::oem::PhysicalEncoder::sample(unsigned char&)',
              'zseries::oem::PhysicalEncoder::interrupt()',
              'millis','esp_timer_get_time','esp_timer_impl_get_time','vTaskEnterCritical','vTaskExitCritical',
              '__digitalWrite','digitalWrite','__digitalRead','digitalRead','__onPinInterrupt',
              '__divdi3','__udivdi3','__umoddi3']
        if args.profile=='standalone':
            names += ['zseries::standalone::Adapter::begin()', 'zseries::standalone::Adapter::poll()',
                      'picobrew::standalone::Engine::begin()', 'picobrew::standalone::Engine::poll()',
                      'pico_http_server_poll', 'picobrew::standalone::esp32::SpiffsFiles::mount()']
            iram += ['zseries::standalone::Adapter::guarded_write(void*, unsigned char, bool)']
        for name in names+iram:
            if name not in demangled:raise ValueError('Concrete application did not link required symbol: '+name)
            required[name]=hex(demangled[name])
        for name in iram:
            if not 0x40000000<=demangled[name]<0x400c0000:raise ValueError('Linked interrupt dependency escaped internal code: '+name)
        owner='zseries::oem::esp32::(anonymous namespace)::active_application'
        if owner not in demangled or not 0x3ff80000<=demangled[owner]<0x40000000:raise ValueError('Interrupt owner pointer escaped internal DRAM')
        required[owner]=hex(demangled[owner])
    if any(not 0x40000000 <= addresses[name] < 0x400c0000 for name in critical):
        raise ValueError('Heater ISR dependency linked into flash: ' + ', '.join(name for name in critical if addresses[name] >= 0x400c0000))
    size = subprocess.check_output([str(binary / 'xtensa-esp32-elf-size'), str(elf)], text=True).strip()
    sections={}
    for line in subprocess.check_output([str(binary/'xtensa-esp32-elf-size'),'-A',str(elf)],text=True).splitlines():
        fields=line.split()
        if len(fields)==3 and fields[0] in ['.iram0.vectors','.iram0.text','.dram0.data','.dram0.bss','.flash.rodata','.flash.text']:
            sections[fields[0]]=dict(bytes=int(fields[1]),address=hex(int(fields[2])))
    stack_frames = []
    for source in sorted(output.glob('*.su')):
        if source.name.startswith('owned_') or source.name=='application-entry.su':
            for line in source.read_text().splitlines():
                fields=line.rsplit('\t',2)
                if len(fields)==3 and fields[1].isdigit():
                    stack_frames.append(dict(function=fields[0],bytes=int(fields[1]),allocation=fields[2]))
    report = dict(schema_version=1, profile=args.profile, entry_sha256=digest(args.entry), elf_sha256=digest(elf), map_sha256=digest(map_file),
                  sdk_archive_sha256=digest(sdk_archive), sdk_objects=evidence,
                  extra_sources=extra_evidence, extra_header_sha256=extra_headers,
                  owned_stack_frames=sorted(stack_frames,key=lambda x:x['bytes'],reverse=True),
                  reconstructed_object_report_sha256=digest(objects / 'build-report.json'),
                  reconstructed_sources=reconstructed_sources, reconstructed_headers=reconstructed_headers,
                  original_linker_script_sha256=digest(original_ld), adapted_linker_script_sha256=digest(owned_ld),
                  iram_libgcc_objects=ISR_LIBGCC_OBJECTS, critical_symbols={name: hex(addresses[name]) for name in critical},
                  required_application_symbols=required, sections=sections,
                  complete_application_linked=application,
                  link_probe=elf.name == 'link-probe.elf', hardware_tested=False, flash_image_produced=False,
                  scope='Complete concrete app_main/Core/Head SDK link when complete_application_linked is true. Required heater, encoder, clock, GPIO, lock and compiler arithmetic symbols are checked in internal code; the active owner pointer is checked in internal DRAM. The owner allocation explicitly requests internal memory. Symbol placement does not exercise concurrency, dynamic allocations, SDK failure/panic paths, flash-disabled hardware behavior or a device partition layout.',
                  compiler=object_report['compiler'], size=size, build_script_sha256=digest(__file__))
    (output / 'link-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(size)
    print('Linked ' + elf.name + '; no partition table, bootloader, or flash image was produced')


if __name__ == '__main__':
    main()
