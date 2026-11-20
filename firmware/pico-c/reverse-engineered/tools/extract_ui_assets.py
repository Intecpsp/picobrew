#!/usr/bin/env python3
"""Recover immutable C display data; verify both preserved OEM envelopes first."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from inspect_oem import REPO, ROOT, VARIANTS, read_variant


def sha(data):
    return hashlib.sha256(data).hexdigest()


def array(name, data):
    rows = ["    " + ", ".join(f"0x{x:02x}" for x in data[i:i + 16]) + ","
            for i in range(0, len(data), 16)]
    return f"const uint8_t {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"


def c_string(data):
    """Emit original bytes, including OEM codepage and UTF-8 inconsistencies."""
    return '"' + ''.join(chr(b) if 32 <= b < 127 and b not in (34, 92)
                         else '\\%03o' % b for b in data) + '"'


def generate():
    images = {name: read_variant(name) for name in VARIANTS}
    reference = REPO / "firmware/pico-s-pro/reverse-engineered/src/ui/assets.c"
    text = reference.read_text()
    assets, excluded, report = {}, [], []
    # Existing names are retained only for byte-identical data actually present
    # in both C variants. A named S/Pro bitmap absent from C is not imported.
    for match in re.finditer(r"const uint8_t (\w+)\[(\d+)\] = \{(.*?)\n\};", text, re.S):
        name, size, body = match.groups()
        data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", body))
        if len(data) != int(size):
            raise ValueError("Malformed archived asset " + name)
        offsets = {variant: image.find(data) for variant, image in images.items()}
        if any(offset < 0 for offset in offsets.values()):
            excluded.append(name)
            continue
        assets[name] = data
        report.append({"name": name, "bytes": len(data), "sha256": sha(data),
                       "occurrence_addresses": {v: f"0x{0x20004000 + n:08x}" for v, n in offsets.items()},
                       "evidence": "Exact immutable byte occurrence in each C payload; names inherited from matching S/Pro data"})

    for name, literal in (("pico_c_menu_left_cap", 0x2000ad98),
                          ("pico_c_menu_up", 0x2000ae70), ("pico_c_menu_down", 0x2000ae74),
                          ("pico_c_menu_single", 0x2000ae78), ("pico_c_menu_multiple", 0x2000ae7c)):
        addresses, values = {}, []
        for variant, raw in images.items():
            pointer = literal - (0x18 if variant == "ssd1309" else 0)
            address = struct.unpack_from("<I", raw, pointer - 0x20004000)[0]
            offset = address - 0x20004000
            if offset < 0 or offset + 30 > len(raw):
                raise ValueError("Menu bitmap outside original payload")
            addresses[variant] = f"0x{address:08x}"
            values.append(raw[offset:offset + 30])
        if values[0] != values[1]:
            raise ValueError("Variant-specific menu bitmap differs")
        assets[name] = values[0]
        report.append({"name": name, "bytes": 30, "sha256": sha(values[0]),
                       "occurrence_addresses": addresses,
                       "evidence": "Original C menu draw routine literal pointers;16x15,row-major,MSB-first"})

    font_records = {}
    for variant, literal in (("ssd1325", 0x20005408), ("ssd1309", 0x2000540C)):
        raw = images[variant]
        def data(address, size):
            offset = address - 0x20004000
            if offset < 0 or offset + size > len(raw):
                raise ValueError("Font address outside original image")
            return raw[offset:offset + size]
        descriptor = struct.unpack("<I", data(literal, 4))[0]
        bitmap, glyphs, first, last, advance = struct.unpack("<IIBBB", data(descriptor, 11))
        if (first, last, advance) != (32, 126, 16):
            raise ValueError("Unexpected C custom-font range")
        glyph_bytes = data(glyphs, (last - first + 1) * 8)
        glyph_values = [struct.unpack_from("<HBBBbbx", glyph_bytes, i * 8) for i in range(last - first + 1)]
        bitmap_size = max(row[0] + (row[1] * row[2] + 7) // 8 for row in glyph_values)
        bitmap_bytes = data(bitmap, bitmap_size)
        font_records[variant] = {"descriptor_address": f"0x{descriptor:08x}",
                                "glyph_address": f"0x{glyphs:08x}", "bitmap_address": f"0x{bitmap:08x}",
                                "glyph_bytes": len(glyph_bytes), "glyph_sha256": sha(glyph_bytes),
                                "bitmap_bytes": len(bitmap_bytes), "bitmap_sha256": sha(bitmap_bytes),
                                "first": first, "last": last, "y_advance": advance}
        if variant == "ssd1325":
            expected_font = (glyph_bytes, bitmap_bytes)
            retained_glyphs, retained_bitmap = glyph_values, bitmap_bytes
        elif expected_font != (glyph_bytes, bitmap_bytes):
            raise ValueError("C display variants have differing custom-font data")

    source = ["/* Immutable assets recovered from both hash-pinned Pico C OEM payloads. */\n#include \"ui_internal.h\"\n"]
    declarations = ["#ifndef PICO_C_UI_ASSETS_H\n#define PICO_C_UI_ASSETS_H\n"]
    for name, data in assets.items():
        source.append(array(name, data))
        declarations.append(f"extern const uint8_t {name}[{len(data)}];\n")
    source.append(array("pico_c_custom_font_bitmap", retained_bitmap))
    source.append("static const PicoGlyph custom_glyphs[95] = {\n" + "\n".join(
        "    {" + ", ".join(str(x) for x in row) + "}," for row in retained_glyphs) + "\n};\n")
    source.append("const PicoFont pico_c_ui_custom_font = {pico_c_custom_font_bitmap, custom_glyphs, 32, 126, 16};\n")
    for name, bitmap, width, height in re.findall(
            r"const PicoBitmap (\w+) = \{(\w+), (\d+), (\d+)\};", text):
        if bitmap in assets:
            source.append(f"const PicoBitmap {name} = {{{bitmap}, {width}, {height}}};\n")
            declarations.append(f"extern const PicoBitmap {name};\n")
    for name, length, prefix in (("pico_ui_digits", 10, "pico_ui_digit_"),
                                 ("pico_ui_setup_images", 8, "pico_ui_setup_")):
        names = [prefix + str(i) for i in range(length)]
        if not all(name in assets for name in names):
            raise ValueError("Missing C image in " + name)
        source.append(f"const uint8_t *const {name}[{length}] = {{" + ", ".join(names) + "};\n")
        declarations.append(f"extern const uint8_t *const {name}[{length}];\n")
    # The C instruction dispatcher has at most one line per language. Its text
    # is a scrolling sentence, not the multi-row S/Pro instruction table.
    instruction_literals = {
        1: [0x20007cb4] * 3, 2: [0x20007cb8] * 3,
        3: [0x20007cbc] * 3, 4: [0x20007cc0] * 3,
        5: [0x20007cc4] * 3, 7: [0x20007cc8] * 3,
        10: [0x20007cd4, 0x20007ccc, 0x20007cd0],
        11: [0x20007ce0, 0x20007cd8, 0x20007cdc],
        12: [0x20007ce4] * 3, 13: [0x20007ce8] * 3,
        14: [0x20007cec] * 3, 15: [0x20007cf0] * 3,
        17: [0x20007cf4] * 3, 18: [0x20007cf8] * 3,
        20: [0x20007cfc] * 3, 21: [0x20007d00] * 3,
        23: [0x20007d0c, 0x20007d04, 0x20007d08],
        24: [0x20007d18, 0x20007d10, 0x20007d14],
    }
    recovered_tables = {}
    for variant, raw in images.items():
        delta = -0x18 if variant == "ssd1309" else 0
        def word(address):
            return struct.unpack_from("<I", raw, address - 0x20004000)[0]
        def string(address):
            offset = address - 0x20004000
            if not 0 <= offset < len(raw):
                raise ValueError("UI string outside original payload")
            return raw[offset:raw.index(0, offset)]
        instructions = [[string(word(a + delta)) for a in instruction_literals[index]]
                        if index in instruction_literals else [None] * 3 for index in range(25)]
        tables = {"pico_c_instruction_text": instructions}
        for name, literal, count, width in (
                ("pico_ui_titles", 0x200124e0, 5, 3),
                ("pico_ui_brew_labels", 0x2001253c, 4, 3),
                ("pico_ui_instruction_names", 0x20007e18, 25, 1)):
            start = word(literal + delta)
            tables[name] = [[string(word(start + (i * width + j) * 4)) for j in range(width)]
                            for i in range(count)]
        recovered_tables[variant] = tables
    if recovered_tables["ssd1325"] != recovered_tables["ssd1309"]:
        raise ValueError("C text table content differs by display variant")
    text_tables = []
    for name, rows in recovered_tables["ssd1325"].items():
        columns = len(rows[0])
        shape = f"[{len(rows)}]" + (f"[{columns}]" if columns > 1 else "")
        source.append(f"const char *const {name}{shape} = {{\n" + "\n".join(
            "    " + ("{" if columns > 1 else "") + ", ".join(
                c_string(value) if value is not None else "NULL" for value in row) +
            ("}" if columns > 1 else "") + "," for row in rows) + "\n};\n")
        declarations.append(f"extern const char *const {name}{shape};\n")
        data = b"".join((value or b"") + b"\0" for row in rows for value in row)
        text_tables.append({"name": name, "rows": len(rows), "columns": columns,
                            "text_bytes": len(data), "text_sha256": sha(data),
                            "evidence": "Original C dispatcher literal pointers and original string tables in both images"})
    declarations.append("#endif\n")
    manifest = {"schema_version": 1, "images": {v: VARIANTS[v]["raw_sha256"] for v in images},
                "reference_source_sha256": sha(reference.read_bytes()), "assets": report,
                "custom_font": font_records, "excluded_s_pro_assets": excluded, "text_tables": text_tables,
                "hardware_tested": False}
    return {ROOT / "src/ui/assets.c": "\n".join(source),
            ROOT / "src/ui/ui_assets.h": "".join(declarations),
            ROOT / "analysis/ui-assets.json": json.dumps(manifest, indent=2) + "\n"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for path, text in generate().items():
        if args.check:
            if not path.exists() or path.read_text() != text:
                raise SystemExit("Recovered asset file differs: " + str(path))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    print("Verified Pico C immutable UI assets and all 95 custom-font glyphs in both variants")


if __name__ == "__main__":
    main()
