"""Pinned SPIFFS formatter helpers; no network access or implicit credentials."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('spiffs_hydrogen', 'spiffs_nucleus', 'spiffs_cache', 'spiffs_gc', 'spiffs_check')
PROFILES = {
    'esp8266-2m': dict(partition_offset=0x100000, partition_bytes=0xFB000,
                       page_bytes=256, block_bytes=8192, metadata_bytes=0,
                       magic_length=False, gc_max_runs=5),
    'esp32-4m': dict(partition_offset=0x200000, partition_bytes=0x200000,
                     page_bytes=256, block_bytes=4096, metadata_bytes=4,
                     magic_length=True, gc_max_runs=10),
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_dependencies(esp8266, esp32=None):
    pin = json.loads((ROOT / 'dependencies/esp8266-adapters.json').read_text())
    checked = {}
    for name, expected in pin['sdk_identity_sha256'].items():
        if '/spiffs/' in name or name.endswith('eagle.flash.2m.ld'):
            if digest(esp8266 / name) != expected:
                raise ValueError('Pinned ESP8266 SPIFFS identity mismatch: ' + name)
            checked['esp8266/' + name] = expected
    if esp32 is not None:
        pin = json.loads((ROOT / 'dependencies/esp32-spiffs-sizing.json').read_text())
        for name, expected in pin['identity_sha256'].items():
            if digest(esp32 / name) != expected:
                raise ValueError('Pinned ESP32 SDK identity mismatch: ' + name)
            checked['esp32/' + name] = expected
    return checked


def archive_member(path, wanted):
    data = Path(path).read_bytes()
    if data[:8] != b'!<arch>\n':
        raise ValueError('Expected a Unix archive')
    offset, names = 8, b''
    while offset < len(data):
        header = data[offset:offset + 60]
        if len(header) != 60 or header[58:] != b'`\n':
            raise ValueError('Invalid archive member')
        length = int(header[48:58])
        value = data[offset + 60:offset + 60 + length]
        if len(value) != length:
            raise ValueError('Truncated archive member')
        name = header[:16].decode('ascii').strip()
        if name == '//':
            names = value
        elif name.startswith('/') and name[1:].isdigit():
            name = names[int(name[1:]):].split(b'/\n', 1)[0].decode('ascii')
        else:
            name = name.rstrip('/')
        if name == wanted:
            return value
        offset += 60 + length + length % 2
    raise ValueError('Pinned archive member is absent: ' + wanted)


def elf_sections(data):
    if data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 94:
        raise ValueError('Expected little-endian ELF32 Xtensa object')
    offset = struct.unpack_from('<I', data, 32)[0]
    entry_size, count, names_index = struct.unpack_from('<HHH', data, 46)
    if entry_size != 40:
        raise ValueError('Unexpected ELF section header size')
    headers = [struct.unpack_from('<10I', data, offset + index * entry_size)
               for index in range(count)]
    names_header = headers[names_index]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    return {names[h[0]:].split(b'\0', 1)[0].decode('ascii'): data[h[4]:h[4] + h[5]]
            for h in headers if h[1] != 8}


def dwarf_layout(text):
    """Read actual SDK DWARF types, including packed member/array extents."""
    entries, parents, current = {}, [], None
    for line in text.splitlines():
        match = re.match(r'\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+(?: \((DW_TAG_\w+)\))?', line)
        if match:
            depth, offset, tag = int(match[1]), int(match[2], 16), match[3]
            parents = parents[:depth]
            current = None
            if tag:
                current = dict(tag=tag, children=[])
                entries[offset] = current
                if parents:
                    parents[-1]['children'].append(current)
                parents.append(current)
            continue
        match = re.search(r'DW_AT_(\w+)\s*:\s*(.*)', line)
        if current is not None and match:
            value = match[2]
            if value.startswith('(indirect string,'):
                value = value.split('): ', 1)[1]
            current[match[1]] = value

    def target(entry):
        return entries[int(re.fullmatch(r'<0x([0-9a-f]+)>', entry['type'])[1], 16)]

    def resolved(name):
        found = [e for e in entries.values() if e.get('name') == name and e['tag'] == 'DW_TAG_typedef']
        if len(found) != 1:
            raise ValueError('Ambiguous or missing SDK DWARF type: ' + name)
        entry = found[0]
        while entry['tag'] in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type'):
            entry = target(entry)
        return entry

    def size(entry):
        if 'byte_size' in entry:
            return int(entry['byte_size'], 0)
        if entry['tag'] == 'DW_TAG_array_type':
            total = size(target(entry))
            for extent in entry['children']:
                total *= int(extent['upper_bound'], 0) + 1
            return total
        return size(target(entry))

    def members(name):
        entry = resolved(name)
        return size(entry), {m['name']: (int(m['data_member_location'], 0), size(target(m)))
                             for m in entry['children'] if m['tag'] == 'DW_TAG_member'}

    page_size, page = members('spiffs_page_header')
    object_size, obj = members('spiffs_page_object_ix_header')
    index_size, index = members('spiffs_page_object_ix')
    return dict(little_endian=True,
        object_id_bytes=size(resolved('spiffs_obj_id')),
        block_index_bytes=size(resolved('spiffs_block_ix')),
        page_index_bytes=size(resolved('spiffs_page_ix')),
        span_index_bytes=size(resolved('spiffs_span_ix')),
        page_header=dict(size=page_size, **{k: v[0] for k, v in page.items()}),
        object_header=dict(size=object_size, p_hdr=obj['p_hdr'][0], _align=obj['_align'][0],
                           file_size=obj['size'][0], type=obj['type'][0], name=obj['name'][0],
                           name_bytes=obj['name'][1], metadata_bytes=obj['meta'][1], meta=obj['meta'][0]),
        index_header=dict(size=index_size, **{k: v[0] for k, v in index.items()}))


def sdk_abi(esp32, readelf, directory):
    """Reproduce the committed on-flash ABI witness from the pinned archive."""
    data = archive_member(esp32 / 'tools/sdk/lib/libspiffs.a', 'spiffs_nucleus.o')
    path = directory / 'spiffs_nucleus.o'
    path.write_bytes(data)
    text = subprocess.check_output([str(readelf), '--debug-dump=info', str(path)], text=True)
    layout = dwarf_layout(text)
    sections = elf_sections(data)
    return dict(archive_member='spiffs_nucleus.o',
                object_sha256=hashlib.sha256(data).hexdigest(), layout=layout,
                source='DWARF packed type sizes/member offsets from the actual pinned SDK object',
                format_function_sha256={name: hashlib.sha256(sections['.text.' + name]).hexdigest()
                    for name in ('spiffs_erase_block', 'spiffs_obj_lu_scan',
                                 'spiffs_object_create', 'spiffs_object_read')})


def compile_formatter(directory, esp8266, profile):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    flags = ['-O2', '-Wall', '-Wextra', '-Werror']
    if profile.startswith('esp32'):
        flags += ['-DSPIFFS_GC_MAX_RUNS=10', '-DSPIFFS_OBJ_META_LEN=4',
                  '-DSPIFFS_USE_MAGIC_LENGTH=1', '-DSPIFFS_COPY_BUFFER_STACK=256',
                  '-DSPIFFS_HAL_CALLBACK_EXTRA=1', '-DSPIFFS_ALIGNED_OBJECT_INDEX_TABLES=0']
    includes = ['-I' + str(esp8266 / 'cores/esp8266')]
    objects = []
    for name in SOURCES:
        obj = directory / (name + '.o')
        subprocess.run(['gcc', '-std=c99'] + flags +
            ['-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-stringop-truncation'] + includes +
            ['-c', str(esp8266 / ('cores/esp8266/spiffs/' + name + '.c')), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = directory / 'spiffs-image'
    subprocess.run(['g++', '-std=c++11'] + flags + includes +
        [str(ROOT / 'tools/spiffs_image_native.cpp')] + objects + ['-o', str(binary)], check=True)
    layout = json.loads(subprocess.check_output([str(binary), '--layout'], text=True))['layout']
    pin = json.loads((ROOT / 'dependencies/spiffs-format-abi.json').read_text())
    expected = pin['layouts']['esp32' if profile.startswith('esp32') else 'esp8266']
    if layout != expected:
        raise ValueError('Native formatter does not match the pinned on-flash ABI')
    return binary


def valid_logical_name(name):
    if not name.startswith('/pb/'):
        return False
    key = name[4:]
    return (bool(re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.]{0,22}', key)) and
            not key.endswith('.') and '..' not in key and not key.endswith(('.tmp', '.bak')))


def staged_files(arguments, partition_bytes):
    result = {}
    for argument in arguments:
        name, separator, source = argument.partition('=')
        if not separator or not valid_logical_name(name):
            raise ValueError('Staged file must have an allowed /pb/ logical name')
        if name in result:
            raise ValueError('Duplicate staged logical name')
        path = Path(source).resolve(strict=True)
        if not path.is_file() or path.stat().st_size > partition_bytes:
            raise ValueError('Staged input is not a bounded regular file')
        data = path.read_bytes()
        if name == '/pb/api.key' and not re.fullmatch(b'[0-9a-f]{64}', data):
            raise ValueError('Staged API key must be exactly 64 lowercase hexadecimal bytes')
        result[name] = (path, data)
    return dict(sorted(result.items()))


def inspect_image(data, profile, expected):
    """Independent on-flash traversal, using the SDK-verified packed ABI.

    Checks magic, lookup/page IDs, finalized header flags and every data/index
    reference. This is separate from the SPIFFS native mount/readback itself.
    """
    geometry = PROFILES[profile]
    if len(data) != geometry['partition_bytes']:
        raise ValueError('Filesystem image length mismatch')
    pin = json.loads((ROOT / 'dependencies/spiffs-format-abi.json').read_text())
    layout = pin['layouts']['esp32' if profile.startswith('esp32') else 'esp8266']
    page_size, block_size = geometry['page_bytes'], geometry['block_bytes']
    page_count, block_count = len(data) // page_size, len(data) // block_size
    per_block, lookup_pages = block_size // page_size, 1
    page_header, object_header, index_header = layout['page_header'], layout['object_header'], layout['index_header']
    pages, headers = {}, {}
    for block in range(block_count):
        base = block * block_size
        magic = (0x529 ^ page_size ^ (block_count - block if geometry['magic_length'] else 0)) & 0xffff
        if struct.unpack_from('<H', data, base + page_size - 4)[0] != magic:
            raise ValueError('Filesystem block magic does not match pinned SDK formula')
        for entry in range(per_block - lookup_pages):
            object_id = struct.unpack_from('<H', data, base + entry * 2)[0]
            if object_id in (0, 0xffff):
                continue
            page = block * per_block + lookup_pages + entry
            start = page * page_size
            owner, span, flags = struct.unpack_from('<HHB', data, start)
            if owner != object_id or flags & 3 or not flags & 0x80:
                raise ValueError('Filesystem lookup/page header mismatch')
            if bool(owner & 0x8000) == bool(flags & 4):
                raise ValueError('Filesystem page index flag mismatch')
            pages[page] = (owner, span, flags)
            if owner & 0x8000 and span == 0:
                if not flags & 0x40:
                    raise ValueError('Filesystem contains a deleted object header')
                length = struct.unpack_from('<I', data, start + object_header['file_size'])[0]
                # SPIFFS represents an untouched zero-length object with -1.
                if length == 0xffffffff:
                    length = 0
                raw_name = data[start + object_header['name']:start + object_header['name'] + object_header['name_bytes']]
                if b'\0' not in raw_name:
                    raise ValueError('Unterminated filesystem name')
                name = raw_name.split(b'\0', 1)[0].decode('ascii')
                if not valid_logical_name(name) or name in headers:
                    raise ValueError('Unexpected or duplicate filesystem name')
                if data[start + object_header['type']] != 1:
                    raise ValueError('Unexpected filesystem object type')
                if geometry['metadata_bytes'] and data[start + object_header['meta']:start + object_header['meta'] + 4] != bytes(4):
                    raise ValueError('Staged metadata is not deterministic epoch zero')
                headers[name] = (page, owner, length)
    if set(headers) != set(expected):
        raise ValueError('Filesystem names differ from staged inputs')
    referenced = set()
    for name, (page, owner, length) in headers.items():
        if length != len(expected[name]):
            raise ValueError('Filesystem object length differs from staged input')
        reconstructed = bytearray()
        header_entries = (page_size - object_header['size']) // 2
        index_entries = (page_size - index_header['size']) // 2
        data_capacity = page_size - page_header['size']
        for span in range((length + data_capacity - 1) // data_capacity):
            if span < header_entries:
                index_page, slot, index_offset = page, span, object_header['size']
            else:
                index_span = 1 + (span - header_entries) // index_entries
                matches = [p for p, value in pages.items() if value[0] == owner and value[1] == index_span]
                if len(matches) != 1:
                    raise ValueError('Missing or duplicate filesystem index page')
                index_page, slot, index_offset = matches[0], (span - header_entries) % index_entries, index_header['size']
                referenced.add(index_page)
            data_page = struct.unpack_from('<H', data, index_page * page_size + index_offset + slot * 2)[0]
            if data_page >= page_count or data_page not in pages or pages[data_page][:2] != (owner & 0x7fff, span):
                raise ValueError('Filesystem data reference mismatch')
            referenced.add(data_page)
            start = data_page * page_size + page_header['size']
            reconstructed += data[start:start + min(data_capacity, length - len(reconstructed))]
        if bytes(reconstructed) != expected[name]:
            raise ValueError('Independent filesystem data reconstruction failed')
        referenced.add(page)
    if referenced != set(pages):
        raise ValueError('Filesystem contains unreferenced live pages')
    tail = block_count * block_size
    if data[tail:] != b'\xff' * (len(data) - tail):
        raise ValueError('Filesystem unused trailing region was modified')
    return dict(blocks=block_count, live_pages=len(pages), files=len(headers),
                unused_tail_bytes=len(data) - tail, exact_contents=True)
