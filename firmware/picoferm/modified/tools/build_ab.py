"""Link real PicoFerm application images at both bank VMAs, and a permanent ROM bootstrap."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parents[1] / 'common/tools'))
from build_esp8266_image import component, read_elf

BANKS = (0x3000, 0x83000)
BANK_SIZE = 0x7d000
SECTIONS = ('.irom0.text', '.text', '.data', '.rodata')
IDENTITY = b'PicoFerm-AB1:'
VERSION = '1.0.0'

def bank_record(bank, image):
    body = struct.pack('<5I', 0x31424650, bank, 1, len(image), zlib.crc32(image))
    return body + struct.pack('<2I', zlib.crc32(body), 0xfffffff8)

def replace_once(text, needle, value):
    if text.count(needle) != 1:
        raise ValueError('Unexpected layout in pinned linker script: ' + needle)
    return text.replace(needle, value)

def prepare_loader(out, arduino, cc):
    pinned = arduino / 'bootloaders/eboot'
    scripts = {
        'bootloaders/eboot/eboot.ld': '60976d360acc25b72d04400a38fc490a2af92ac539c191e3f4dc4444997d6fd0',
        'bootloaders/eboot/rom.ld': 'f6215696f53b49db451c80dca2a8336f778c85825376c50e7f8f8abc346c8924',
        'tools/sdk/ld/eagle.flash.2m.ld': '2fd3ad6c55a34c7709c25e4009f1179a61a973e3f5bd5d6fc85dbaccfa7164a1',
        'tools/sdk/ld/eagle.app.v6.common.ld': 'e40f0ba1cde04b22f6717cbbed51482154cfbf25bbe01ef529247f19022d946e',
    }
    for name, sha in scripts.items():
        if hashlib.sha256((arduino/name).read_bytes()).hexdigest() != sha:
            raise ValueError('PicoFerm A/B linker differs from Arduino 2.4.1: '+name)
    source = ROOT / 'platform/ab'

    elf = out / 'bootstrap.elf'
    subprocess.run([cc, '-std=gnu99', '-Os', '-nostdlib', '-ffunction-sections',
                    '-fdata-sections', '-mlongcalls', '-mno-text-section-literals',
                    '-Wl,--gc-sections', '-Wl,-umain', '-T', str(pinned/'eboot.ld'),
                    str(source/'boot.c'), str(source/'loader.c'), '-o', elf],
                   cwd=pinned, check=True)
    entry, sections = read_elf(elf)
    if set(sections) != {'.text'} or sections['.text']['address'] != 0x4010f000:
        raise ValueError('Bootstrap depends on unexpected memory')
    boot = component(entry, [sections['.text']])
    if len(boot) > 4096:
        raise ValueError('Bootstrap would overlap the boot records')
    (out/'bootstrap.bin').write_bytes(boot)

    sha = hashlib.sha256(boot).digest()
    (out/'bootstrap_identity.h').write_text(
        '#define FERM_LOADER_LENGTH %du\n' % len(boot) +
        'static const unsigned char ferm_loader_sha[32] = {' +
        ','.join(str(b) for b in sha) + '};\n')

def build(out, arduino, cc, link):
    sdk = arduino / 'tools/sdk'
    boot = (out/'bootstrap.bin').read_bytes()
    app_ld = (sdk/'ld/eagle.app.v6.common.ld').read_text()
    app_ld = replace_once(app_ld, '_irom0_text_start = ABSOLUTE(.);',
                          '_irom0_text_start = ABSOLUTE(.); KEEP(*(.ferm_ota_identity))')
    common = out/'app-ab.ld'
    common.write_text(app_ld)
    manifest = {'schema_version': 1, 'protocol': 'esp8266-ab-v1', 'model': 'picoferm', 'version': VERSION,
                'bootstrap_sha256': hashlib.sha256(boot).hexdigest(), 'images': []}
    for bank, base in enumerate(BANKS):
        script = (sdk/'ld/eagle.flash.2m.ld').read_text()
        script = replace_once(script, '0x40201010, len = 0xfeff0',
                              f'{0x40200010 + base:#x}, len = {BANK_SIZE - 16:#x}')
        script = replace_once(script, '../ld/eagle.app.v6.common.ld', str(common))
        layout = out / f'bank-{bank}.ld'
        layout.write_text(script)
        elf = out / f'bank-{bank}.elf'
        args = ['-u', 'picoferm_ota_identity', *link]
        args[args.index('-T')+1] = str(layout)
        args[args.index('-o')+1] = str(elf)
        args = ['-Wl,-Map='+str(elf.with_suffix('.map')) if a.startswith('-Wl,-Map=') else a for a in args]
        subprocess.run([cc, *args], cwd=sdk/'lib', check=True)
        entry, sections = read_elf(elf)
        image = component(entry, [sections[name] for name in SECTIONS])
        if set(sections) != set(SECTIONS) or len(image) > BANK_SIZE:
            raise ValueError('Application exceeds its intended bank or omits a section')
        offset = 8
        for name in SECTIONS:
            address, length = struct.unpack_from('<II', image, offset)
            offset += 8
            if address != sections[name]['address'] or image[offset:offset+length] != sections[name]['data']:
                raise ValueError('Application bytes differ from the original ELF')
            if name == '.irom0.text' and address != 0x40200000 + base + offset:
                raise ValueError('Instruction flash mapping does not match its physical bank')
            offset += length
        if image[16:48].rstrip(b'\0') != IDENTITY + VERSION.encode():
            raise ValueError('PicoFerm application identity missing at the front of mapped flash')
        filename = f'bank-{bank}.bin'
        (out/filename).write_bytes(image)
        manifest['images'].append({'bank': bank, 'offset': base, 'size': len(image),
                                   'file': filename, 'sha256': hashlib.sha256(image).hexdigest()})
        if bank == 0:
            (out/'initial-record.bin').write_bytes(bank_record(0, image))
    (out/'ota-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Verified both ELF-to-image mappings and the sector-zero bootstrap', flush=True)
