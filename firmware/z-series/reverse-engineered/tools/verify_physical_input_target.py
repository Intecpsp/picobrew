#!/usr/bin/env python3
"""Check the reconstructed ISR path and pinned SDK GPIO reader's IRAM sections."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objects', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--arduino', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); build = json.loads((args.objects / 'build-report.json').read_text())
    compiler = args.toolchain / 'bin/xtensa-esp32-elf-gcc'
    objdump = args.toolchain / 'bin/xtensa-esp32-elf-objdump'
    gpio = args.arduino / 'cores/esp32/esp32-hal-gpio.c'; gpio_object = args.objects / 'sdk_gpio_isr.o'
    pin = next(p for p in json.loads((ROOT / 'dependencies/esp32.json').read_text())['packages'] if p['name'] == 'arduino-esp32')
    archive = args.arduino.parent / pin['archive']
    if archive.stat().st_size != pin['archive_bytes'] or digest(archive) != pin['archive_sha256']:
        raise SystemExit('Use the exact pinned SDK acquisition archive')
    with tarfile.open(archive) as original:
        for relative in ['cores/esp32/esp32-hal-gpio.c', 'tools/sdk/ld/esp32.peripherals.ld']:
            member = original.extractfile(pin['directory'] + '/' + relative)
            if member is None or member.read() != (args.arduino / relative).read_bytes():
                raise SystemExit('SDK GPIO source differs from pinned archive: ' + relative)
    includes = [args.arduino / 'cores/esp32', args.arduino / 'variants/esp32']
    includes += sorted(p for p in (args.arduino / 'tools/sdk/include').iterdir() if p.is_dir())
    subprocess.run([str(compiler), '-std=gnu99'] + build['common_flags'] + ['-I' + str(p) for p in includes] +
                   ['-c', str(gpio), '-o', str(gpio_object)], check=True)
    def symbols(path):
        result = {}
        for line in subprocess.check_output([str(objdump), '-t', str(path)], text=True).splitlines():
            fields = line.split()
            if len(fields) >= 6 and 'F' in fields:
                result[fields[-1]] = dict(address=int(fields[0], 16), section=fields[-3], bytes=int(fields[-2], 16))
        return result
    source = args.objects / 'src_physical_inputs.cpp.o'
    adapter = args.objects / 'platform_esp32_physical_input_platform.cpp.o'
    entries = [(source, 'PhysicalEncoder6sample'), (source, 'PhysicalEncoder9interrupt'),
               (adapter, '14encoder_change'), (adapter, '8read_pin'), (adapter, '14button_falling'),
               (gpio_object, '__digitalRead'), (gpio_object, 'digitalRead'), (gpio_object, '__onPinInterrupt')]
    checked = []
    for path, fragment in entries:
        matches = [(name, row) for name, row in symbols(path).items() if name == fragment or fragment in name]
        if fragment == 'digitalRead': matches = [(name, row) for name, row in matches if name == fragment]
        if len(matches) != 1: raise SystemExit('Ambiguous/missing ISR symbol: ' + fragment)
        name, row = matches[0]
        if not row['section'].startswith('.iram'): raise SystemExit('ISR symbol escaped IRAM: ' + name)
        checked.append(dict(object=path.name, symbol=name, **row))
    sdk_symbols = symbols(gpio_object)
    if sdk_symbols['digitalRead'] != sdk_symbols['__digitalRead']:
        raise SystemExit('Pinned SDK digitalRead alias changed')
    if 'ESP_INTR_FLAG_IRAM' not in gpio.read_text():
        raise SystemExit('Pinned GPIO interrupt registration lost its IRAM flag')
    gpio_disassembly = subprocess.check_output([str(objdump), '-dr', '-j', sdk_symbols['digitalRead']['section'], str(gpio_object)], text=True)
    calls = re.findall(r'\bcall(?:0|4|8|12|x0|x4|x8|x12)\b', gpio_disassembly)
    if calls: raise SystemExit('GPIO read gained an external call')
    irq_disassembly = subprocess.check_output([str(objdump), '-dr', '-j', '.iram1', str(source)], text=True)
    if len(re.findall(r'\bmemw\b', irq_disassembly)) < 2:
        raise SystemExit('Volatile ISR stores lost Xtensa memory ordering')
    linker = args.arduino / 'tools/sdk/ld/esp32.peripherals.ld'
    if not re.search(r'GPIO\s*=\s*0x3ff44000', linker.read_text(), re.IGNORECASE):
        raise SystemExit('Pinned GPIO peripheral address changed')
    selected = ['src/physical_inputs.cpp', 'platform/esp32/physical_input_platform.cpp']
    owned = [row for row in build['sources'] if row['source'] in selected]
    for row in owned:
        if row['source_sha256'] != digest(ROOT / row['source']): raise SystemExit('Object source identity differs')
    report = dict(schema_version=1, compiler=build['compiler'], compiler_sha256=build['compiler_sha256'],
                  arduino_reference=build['arduino_reference'], isr_sections_verified=True, isr_symbols=checked,
                  owned_sources=owned, owned_header_sha256={p: digest(ROOT / p) for p in
                      ['include/zseries/physical_inputs.hpp', 'platform/esp32/physical_input_platform.hpp']},
                  checker_sha256=digest(__file__), sdk_gpio_source_sha256=digest(gpio), sdk_gpio_object_sha256=digest(gpio_object),
                  sdk_archive_sha256=pin['archive_sha256'],
                  sdk_gpio_read=dict(calls=calls, peripheral_address='0x3ff44000', linker_sha256=digest(linker),
                                     disassembly_sha256=hashlib.sha256(gpio_disassembly.encode()).hexdigest()),
                  volatile_memory_barriers=len(re.findall(r'\bmemw\b', irq_disassembly)),
                  complete_application_linked=False, hardware_tested=False,
                  scope='Historical target objects place both encoder ISR callbacks, portable interrupt/sample bodies and concrete GPIO callback in IRAM. The pinned SDK digitalRead alias is in IRAM and directly reads the mapped GPIO peripheral without calls. Application-owned binding/encoder storage must remain in internal RAM; actual concurrent interrupts and cache-disabled hardware operation are not exercised.')
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('Verified eight ISR-path symbols in IRAM, direct SDK GPIO read and volatile MEMW stores')

if __name__ == '__main__': main()
