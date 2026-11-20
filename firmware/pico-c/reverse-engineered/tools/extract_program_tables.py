#!/usr/bin/env python3
"""Recover program constants directly from both pinned C OEM payloads."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from inspect_oem import IMAGE_BASE, VARIANTS, read_variant

ROOT = Path(__file__).resolve().parents[1]
# These are pointer-bearing literal addresses in the C program initializer,
# debug-name helpers and instruction callers, not guessed data scans.
TABLES = [
    ('program_titles', 0x20004e4c, 21, 'strings', True),
    ('program_names', 0x20004e50, 21, 'strings', True),
    ('program_states', 0x20004e54, 21, 'strings', True),
    ('program_logos', 0x20004e58, 21, 'strings', True),
    ('program_interrupted', 0x20004e5c, 21, 'strings', True),
    ('program_names_debug', 0x20004aa8, 7, 'strings', False),
    ('program_session_names', 0x20004a9c, 6, 'strings', False),
    ('actions_brewing', 0x20004e74, 6, 'words', False),
    ('actions_cleaning', 0x20004e78, 5, 'words', False),
    ('actions_sousvide', 0x20004e7c, 5, 'words', False),
    ('actions_still', 0x20004e80, 5, 'words', False),
    ('actions_coffee', 0x20004e84, 5, 'words', False),
    ('actions_manual', 0x20004e88, 3, 'words', False),
]
INSTRUCTIONS = [
    ('manual_setup', 0x20011310, 3),
    ('manual_cleanup', 0x2001134c, 3),
    ('sousvide_setup', 0x20011a10, 3),
    ('coffee_setup', 0x20011a14, 3),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    report = {'schema_version': 1, 'variants': {}}
    recovered = {}
    for variant in VARIANTS:
        raw = read_variant(variant)

        def words(address, count):
            offset = address - IMAGE_BASE
            if offset < 0 or offset + count * 4 > len(raw):
                raise ValueError('Table outside preserved C image')
            return list(struct.unpack_from('<' + 'I' * count, raw, offset))

        def text(address):
            offset = address - IMAGE_BASE
            if not 0 <= offset < len(raw):
                raise ValueError('String outside preserved C image')
            end = raw.find(b'\0', offset, offset + 256)
            if end < 0:
                raise ValueError('Unterminated program string')
            return raw[offset:end].decode('ascii')

        tables = {}
        for name, literal, count, kind, grouped in TABLES:
            location = literal + (4 if variant == 'ssd1309' else 0)
            address = words(location, 1)[0]
            values = words(address, count)
            decoded = list(map(text, values)) if kind == 'strings' else values
            tables[name] = {'literal_address': hex(location), 'table_address': hex(address),
                            'bytes_sha256': hashlib.sha256(raw[address - IMAGE_BASE:
                                                              address - IMAGE_BASE + count * 4]).hexdigest(),
                            'values': decoded}
            if kind == 'strings':
                tables[name]['string_addresses'] = list(map(hex, values))
            previous = recovered.setdefault(name, decoded)
            if previous != decoded:
                raise ValueError(f'{name}: display variants have different program constants')
        for name, literal, count in INSTRUCTIONS:
            location = literal - (24 if variant == 'ssd1309' else 0)
            address = words(location, 1)[0]
            tables[name] = {'literal_address': hex(location), 'table_address': hex(address),
                            'values': words(address, count)}
        report['variants'][variant] = {'raw_sha256': VARIANTS[variant]['raw_sha256'],
                                        'tables': tables}

    lines = ['/* Recovered directly from both Pico C OEM images by tools/extract_program_tables.py.',
             ' * Address and input provenance: analysis/program-tables.json. */', '#include <stdint.h>', '']
    for name, literal, count, kind, grouped in TABLES:
        values = recovered[name]
        if kind == 'strings':
            declaration = 'static const char *const ' + name + ('[7][3]' if grouped else '[]')
            lines.append(declaration + ' = {')
            if grouped:
                for i in range(0, count, 3):
                    lines.append('    {' + ', '.join(json.dumps(s) for s in values[i:i + 3]) + '},')
            else:
                lines.append('    ' + ', '.join(json.dumps(s) for s in values))
            lines.append('};')
        else:
            lines.append('static const uint32_t ' + name + '[] = {' +
                         ', '.join(str(x) + 'u' for x in values) + '};')
    products = {ROOT / 'src/recipe_data.inc': '\n'.join(lines) + '\n',
                ROOT / 'analysis/program-tables.json': json.dumps(report, indent=2) + '\n'}
    for path, content in products.items():
        if args.check:
            if not path.exists() or path.read_text() != content:
                raise ValueError('Generated C program constants changed: ' + str(path))
        else:
            path.write_text(content)
    print(f'{len(TABLES)} program tables and {len(INSTRUCTIONS)} instruction sequences checked in both C variants')


if __name__ == '__main__':
    main()
