#!/usr/bin/env python3
"""Recover Z119 localized labels, screen bodies and non-font bitmap assets.

Only the preserved image is needed. Screen mappings are decoded from the
original switch jump table and byte-checked L32R/load/store return bodies;
generated decompiler text is not an input or a runtime dependency.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from analyze_images import parse_image

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"
# Category, original diagnostic type, getter, pointer table, enum count.
TEXT = [
    ("TestEnum", "TestEnum", 0x400d2264, 0x3f4287cc, 3),
    ("PauseReason", "PauseReason", 0x400d22fc, 0x3f4286f4, 18),
    ("SessionType", "SessionType", 0x400d2398, 0x3f428628, 17),
    ("RuntimeStillStateId", "RuntimeStillStateId", 0x400d2464, 0x3f4285bc, 9),
    ("InfoScreenType", "InfoScreenType", 0x400d2500, 0x3f428058, 19),
    ("TextInfo", "Text_Info", 0x400d259c, 0x3f427fb0, 14),
    ("TextCommon", "Text_Common", 0x400d2648, 0x3f427e54, 29),
    ("TextTime", "Text_Time", 0x400d2710, 0x3f427db8, 13),
    ("TextTitle", "Text_Title", 0x400d27ac, 0x3f427ac4, 63),
    ("TextAbout", "Text_About", 0x400d2858, 0x3f427a28, 13),
    ("TextAzure", "Text_Azure", 0x400d28f4, 0x3f427a04, 3),
    ("TextSessions", "Text_Sessions", 0x400d298c, 0x3f4279e0, 3),
    ("TextInput", "Text_Input", 0x400d2a24, 0x3f4279b0, 4),
    ("TextPause", "Text_Pause", 0x400d2acc, 0x3f42795c, 7),
    ("TextEeprom", "Text_EEPROM", 0x400d2b74, 0x3f4278fc, 8),
    ("SlideshowAutoCycle", "SlideshowAutoCycle", 0x400d2c0c, 0x3f4278cc, 4),
    ("Units", "Units", 0x400d2ca4, 0x3f4278b4, 2),
    ("UiCommand", "UiCommand", 0x400d2d3c, 0x3f426db4, 176),
]
BITMAPS = [
    ("LogoLarge", 0x3f42a4fc, 0x400e7b90),
    ("LogoSmall", 0x3f42a954, 0x40103858),
    ("DiagnosticChecker18", 0x3f42aa5c, 0x400e78dc),
    ("DiagnosticGradient16", 0x3f42ab24, 0x400e78f0),
    ("DiagnosticChecker16A", 0x3f42abc8, 0x400e78c8),
    ("DiagnosticChecker16B", 0x3f42ac6c, 0x400e78b4),
    ("DiagnosticBitmap64x32", 0x3f42ad10, 0x400e780c),
    ("TimeSeparatorLarge", 0x3f42b134, 0x400e85f4),
    ("DiagnosticEllipse30x8", 0x3f42b180, 0x400ea5a4),
    ("SelectionSingle", 0x3f42b21c, 0x400e9750),
    ("DiagnosticEllipse10x8", 0x3f42b25c, 0x400ea5a4),
    ("TimeSeparatorSmall", 0x3f42b2a8, 0x400e85f4),
    ("SelectionBullet", 0x3f42b2d8, 0x400e9750),
    ("SelectionUp", 0x3f4424d0, 0x400e9750),
    ("SelectionDown", 0x3f442500, 0x400e9750),
    ("SelectionBoth", 0x3f442530, 0x400e9750),
]
SCREEN_JUMP_TABLE = 0x3f41f0c0
SCREEN_COUNT = 103


def sha(data):
    return hashlib.sha256(data).hexdigest()


def c_string(data):
    """Readable UTF-8 C++ literal, preserving control code points byte exactly."""
    result = '"'
    for char in data.decode("utf-8"):
        if char in ('"', "\\"):
            result += "\\" + char
        elif char == "\n":
            result += "\\n"
        elif char == "\r":
            result += "\\r"
        elif char == "\t":
            result += "\\t"
        elif ord(char) < 32 or 127 <= ord(char) < 160:
            result += "".join(f"\\{byte:03o}" for byte in char.encode("utf-8"))
        else:
            result += char
    return result + '"'


def recover():
    raw = (ROOT.parent / "oem/upstream/zseries_0_0_119.bin").read_bytes()
    if sha(raw) != IMAGE_SHA:
        raise ValueError("Preserved Z119 image hash differs")
    image = parse_image(raw)

    def read(address, size, kind=None):
        for segment in image["segments"]:
            start = segment["load_address"]
            if (kind is None or segment["memory_kind"] == kind) and start <= address and address + size <= start + segment["size_bytes"]:
                offset = segment["file_offset"] + address - start
                return raw[offset:offset + size]
        raise ValueError(f"Unmapped or wrong-kind OEM range 0x{address:08x}+{size}")

    def word(address):
        return struct.unpack("<I", read(address, 4))[0]

    strings = {}

    def string(address):
        if address not in strings:
            data = bytearray()
            for offset in range(4096):
                byte = read(address + offset, 1, "DROM")[0]
                if not byte:
                    break
                data.append(byte)
            else:
                raise ValueError("Unterminated OEM label")
            data.decode("utf-8")
            strings[address] = bytes(data)
        return address

    groups = []
    for category, name, getter, table, count in TEXT:
        pointer_bytes = read(table, count * 12, "DROM")
        pointers = [string(p) for p in struct.unpack(f"<{count * 3}I", pointer_bytes)]
        groups.append(dict(category=category, original_type=name, getter=f"0x{getter:08x}", table=f"0x{table:08x}",
                           count=count, table_sha256=sha(pointer_bytes), pointers=pointers))

    screens = []
    for index in range(1, SCREEN_COUNT):
        entry = word(SCREEN_JUMP_TABLE + index * 4)
        body = read(entry, 12, "IROM")
        if body[0] != 0x31 or body[3:] != bytes.fromhex("3055a0380539041df0"):
            raise ValueError(f"Screen {index} no longer has the reviewed lookup instruction pattern")
        displacement = int.from_bytes(body[1:3], "little", signed=True)
        if displacement >= 0:
            raise ValueError("Unreviewed L32R literal displacement")
        literal = ((entry + 3) & ~3) + displacement * 4
        table = word(literal)
        pointers = [string(word(table + language * 4)) for language in range(3)]
        screens.append(dict(id=index, entry=f"0x{entry:08x}", body_sha256=sha(body),
                            literal=f"0x{literal:08x}", table=f"0x{table:08x}", pointers=pointers))

    bitmaps = []
    for name, address, caller in BITMAPS:
        descriptor = read(address, 36, "DROM")
        width, height, stride, left, top, right, bottom, pixels_address, size = struct.unpack("<9I", descriptor)
        if not (0 < width <= 128 and 0 < height <= 64 and stride == (width + 1) // 2 and
                (left, top, right, bottom, size) == (0, 0, width - 1, height - 1, stride * height)):
            raise ValueError(f"Invalid packed bitmap {name}")
        pixels = read(pixels_address, size, "DROM")
        bitmaps.append(dict(name=name, address=f"0x{address:08x}", caller=f"0x{caller:08x}",
                            descriptor_sha256=sha(descriptor), width=width, height=height, row_bytes=stride,
                            pixels_address=f"0x{pixels_address:08x}", pixels_bytes=size,
                            pixels_sha256=sha(pixels), pixels=pixels))
    return groups, screens, bitmaps, strings


def outputs():
    groups, screens, bitmaps, strings = recover()
    lines = ["// Generated by tools/generate_ui_assets.py from the preserved Z119 image.",
             "// Original UTF-8 bytes, table indices and packed pixels; no OEM image needed at runtime.",
             '#include "zseries/ui_catalog.hpp"', "namespace zseries { namespace oem {", "namespace {"]
    for address, data in sorted(strings.items()):
        lines.append(f"const char text_{address:08x}[] = {c_string(data)};")
    for group in groups:
        lines.append(f"const char* const {group['category']}_text[][3] = {{")
        for index in range(group["count"]):
            pointers = group["pointers"][index * 3:index * 3 + 3]
            lines.append("    {" + ", ".join(f"text_{a:08x}" for a in pointers) + f"}}, // {index}")
        lines.append("};")
    lines.append("const char* const screen_text[][3] = {")
    for screen in screens:
        lines.append("    {" + ", ".join(f"text_{a:08x}" for a in screen["pointers"]) + f"}}, // ScreenId {screen['id']}")
    lines.append("};")
    for bitmap in bitmaps:
        name, pixels = bitmap["name"], bitmap["pixels"]
        lines.append(f"const std::uint8_t {name}_pixels[] = {{")
        for index in range(0, len(pixels), 24):
            lines.append("    " + ",".join(f"0x{byte:02x}" for byte in pixels[index:index + 24]) + ",")
        lines.extend(["};", f"const DisplayBitmap {name}_bitmap{{{bitmap['width']}, {bitmap['height']}, {bitmap['row_bytes']}, "
                      f"DisplayRect{{0, 0, {bitmap['width'] - 1}, {bitmap['height'] - 1}}}, {name}_pixels, sizeof {name}_pixels}};"])
    lines.extend(["} // namespace", "", "std::size_t ui_text_count(UiTextCategory category) noexcept {", "    switch (category) {"])
    for group in groups:
        lines.append(f"    case UiTextCategory::{group['category']}: return {group['count']};")
    lines.extend(["    }", "    return 0;", "}", "", "const char* ui_localized_text(UiTextCategory category, std::uint32_t item, LanguageType language) noexcept {",
                  "    const std::uint32_t requested = static_cast<std::uint32_t>(language);", "    const std::uint32_t lang = requested < 3 ? requested : 0;", "    switch (category) {"])
    for group in groups:
        lines.append(f"    case UiTextCategory::{group['category']}: return item < {group['count']} ? {group['category']}_text[item][lang] : \"<{group['original_type']}::INVALID>\";")
    lines.extend(["    }", '    return "<UiTextCategory::INVALID>";', "}", "", "UiScreenTextResult ui_screen_text(ScreenId screen, LanguageType language, const char*& text) noexcept {",
                  "    const std::uint32_t id = static_cast<std::uint32_t>(screen);", "    if (id == 0 || id >= 103) return UiScreenTextResult::Unavailable;",
                  "    const std::uint32_t lang = static_cast<std::uint32_t>(language);", "    if (lang >= 3) return UiScreenTextResult::InvalidLanguage;",
                  "    text = screen_text[id - 1][lang];", "    return UiScreenTextResult::Ok;", "}", "", "const DisplayBitmap* ui_bitmap(UiBitmapId id) noexcept {", "    switch (id) {"])
    for bitmap in bitmaps:
        lines.append(f"    case UiBitmapId::{bitmap['name']}: return &{bitmap['name']}_bitmap;")
    lines.extend(["    }", "    return nullptr;", "}", "}} // namespace zseries::oem", ""])
    source = "\n".join(lines)
    for group in groups:
        group["pointers"] = [f"0x{x:08x}" for x in group["pointers"]]
    for screen in screens:
        screen["pointers"] = [f"0x{x:08x}" for x in screen["pointers"]]
    for bitmap in bitmaps:
        del bitmap["pixels"]
    report = dict(schema_version=1, input_sha256=IMAGE_SHA, languages=["English", "French", "Spanish"],
                  scope="Exact original string and pixel extraction with byte-checked screen jump-table decoding; application behavior validated separately.",
                  original_vendor_asset_names_recovered=False, label_tables=groups, screen_getter="0x400d9a2c",
                  screen_jump_table=f"0x{SCREEN_JUMP_TABLE:08x}", screens=screens, bitmaps=bitmaps,
                  strings=[dict(address=f"0x{address:08x}", bytes=len(data), sha256=sha(data + b"\0")) for address, data in sorted(strings.items())],
                  generated_source_sha256=sha(source.encode("utf-8")), hardware_tested=False)
    return source, json.dumps(report, indent=2) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    source, report = outputs()
    for path, content in [(ROOT / "src/ui_catalog.cpp", source), (ROOT / "analysis/ui-assets.json", report)]:
        if options.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                raise SystemExit("Stale UI extraction: " + str(path))
        else:
            path.write_text(content, encoding="utf-8")
    print("Verified" if options.check else "Recovered", "18 localized categories, 102 screen bodies and 16 non-font bitmaps")


if __name__ == "__main__":
    main()
