#!/usr/bin/env python3
"""Reproduce bounded libgcc symbol matches against Z119 using ELF relocations.

This verifies the recorded evidence, not complete library equivalence. All
matching bytes outside an explicit relocation mask must agree at one unique
location in the original IROM segment. No third-party Python packages needed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import analyze_images

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def symbol_window(data, name):
    if data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 94:
        raise ValueError("Expected little-endian ELF32 Xtensa object")
    section_offset = struct.unpack_from("<I", data, 32)[0]
    section_size, section_count = struct.unpack_from("<HH", data, 46)
    if section_size != 40 or section_offset + section_count * 40 > len(data):
        raise ValueError("Invalid section table")
    sections = [struct.unpack_from("<10I", data, section_offset + 40 * i) for i in range(section_count)]
    matches = []
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        if section[9] != 16:
            raise ValueError("Unexpected symbol record size")
        for offset in range(section[4], section[4] + section[5], 16):
            start, value, size, info, other, index = struct.unpack_from("<IIIBBH", data, offset)
            text = names[start:names.index(0, start)].decode("ascii")
            if text == name:
                matches.append((value, size, index))
    if len(matches) != 1:
        raise ValueError(f"Expected one defined symbol: {name}")
    start, size, index = matches[0]
    section = sections[index]
    # The original evidence used up to128 bytes bounded by the text section,
    # rather than inferred symbol sizes (several assembler aliases have0).
    window = data[section[4] + start:section[4] + min(start + 128, section[5])]
    relocations = []
    for relocation_section in sections:
        if relocation_section[1] != 4 or relocation_section[7] != index:
            continue
        if relocation_section[9] != 12:
            raise ValueError("Unexpected RELA record size")
        for position in range(relocation_section[4], relocation_section[4] + relocation_section[5], 12):
            offset, info, addend = struct.unpack_from("<IIi", data, position)
            if start <= offset < start + len(window):
                relocations.append([offset - start, info & 255])
    return start, window, relocations


def unique_match(image, window, relocations):
    mask = [True] * len(window)
    for offset, kind in relocations:
        if kind not in (1, 20):
            raise ValueError(f"Unsupported relocation type {kind}")
        width = 4 if kind == 1 else 3
        mask[offset:min(offset + width, len(mask))] = [False] * min(width, len(mask) - offset)
    runs, start = [], None
    for index, included in enumerate(mask + [False]):
        if included and start is None:
            start = index
        elif not included and start is not None:
            runs.append((start, index))
            start = None
    first, last = max(runs, key=lambda pair: pair[1] - pair[0])
    if last - first < 4 or sum(mask) < 32:
        raise ValueError("Insufficient unrelocated bytes for bounded matching")
    anchor = window[first:last]
    matches, cursor = [], 0
    while True:
        found = image.find(anchor, cursor)
        if found < 0:
            break
        candidate = found - first
        if 0 <= candidate and candidate + len(window) <= len(image):
            if all(not included or image[candidate + i] == window[i] for i, included in enumerate(mask)):
                matches.append(candidate)
        cursor = found + 1
    if len(matches) != 1:
        raise ValueError(f"Expected one unique code-window match, found {len(matches)}")
    return matches[0], sum(mask)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True,
                        help="Directory containing bin/xtensa-esp32-elf-gcc and bin/xtensa-esp32-elf-ar")
    parser.add_argument("--catalog", type=Path, default=ROOT / "analysis/math-symbols.json")
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text())
    image = (ROOT.parent / "oem/upstream/zseries_0_0_119.bin").read_bytes()
    if digest(image) != catalog["input_sha256"]:
        raise SystemExit("Original image identity mismatch")
    metadata = analyze_images.parse_image(image)
    segments = [s for s in metadata["segments"] if s["load_address"] == 0x400d0018]
    if len(segments) != 1:
        raise SystemExit("Expected original IROM segment")
    segment = segments[0]
    code = image[segment["file_offset"]:segment["file_offset"] + segment["size_bytes"]]
    compiler = args.toolchain / "bin/xtensa-esp32-elf-gcc"
    archive_tool = args.toolchain / "bin/xtensa-esp32-elf-ar"
    archive = Path(subprocess.check_output([str(compiler), "-print-libgcc-file-name"], text=True).strip())
    if digest(archive.read_bytes()) != catalog["sdk_archive_sha256"]:
        raise SystemExit("Pinned libgcc archive identity mismatch")
    members = {}
    for item in catalog["symbols"]:
        name = item["archive_member"]
        if name not in members:
            members[name] = subprocess.check_output([str(archive_tool), "p", str(archive), name])
        data = members[name]
        if digest(data) != item["member_sha256"]:
            raise SystemExit(f"Object identity mismatch: {name}")
        start, window, relocations = symbol_window(data, item["symbol"])
        match, unrelocated = unique_match(code, window, relocations)
        actual = (start, len(window), sorted(relocations), unrelocated, segment["load_address"] + match)
        expected = (item["symbol_section_offset"], item["compared_bytes"], sorted(item["masked_relocations"]),
                    item["unrelocated_bytes_matched"], item["address"])
        if actual != expected:
            raise SystemExit(f"Evidence mismatch for {item['symbol']}: {actual} != {expected}")
    print(f"Reproduced {len(catalog['symbols'])} unique bounded libgcc symbol matches; full equivalence not claimed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
