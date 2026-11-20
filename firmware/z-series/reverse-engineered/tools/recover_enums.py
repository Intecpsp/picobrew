#!/usr/bin/env python3
"""Build editable typed enum source from reviewed, address-traceable Z tables.

The catalog in analysis/enums.json records the observed bounds in original
Xtensa functions. This generator independently rereads every table pointer and
string from the preserved image before producing source with owned tables.
The resulting C++ has no original-image or original-RAM dependency.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import analyze_images

ROOT = Path(__file__).resolve().parents[1]


def mapped_offset(image: dict, address: int, size: int) -> int:
    for segment in image["segments"]:
        start = segment["load_address"]
        if start <= address and address + size <= start + segment["size_bytes"]:
            return segment["file_offset"] + address - start
    raise ValueError(f"unmapped table/string address 0x{address:x}")


def validated_catalog() -> dict:
    catalog = json.loads((ROOT / "analysis/enums.json").read_text())
    data = (ROOT.parent / "oem/upstream" / catalog["input"]).read_bytes()
    if hashlib.sha256(data).hexdigest() != catalog["input_sha256"]:
        raise ValueError("enum catalog input identity mismatch")
    image = analyze_images.parse_image(data)
    names = set()
    for enum in catalog["enums"]:
        name = enum["name"]
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or name in names:
            raise ValueError("invalid or duplicate enum name")
        names.add(name)
        count = enum["count"]
        if not 1 <= count <= 256 or len(enum["values"]) != count:
            raise ValueError("invalid enum extent")
        table = mapped_offset(image, enum["table_address"], count * 4)
        identifiers = set()
        for index, value in enumerate(enum["values"]):
            pointer = struct.unpack_from("<I", data, table + index * 4)[0]
            if value["value"] != index or pointer != value["string_address"]:
                raise ValueError(f"{name} pointer/value mismatch")
            start = mapped_offset(image, pointer, 1)
            end = data.find(b"\0", start, start + 256)
            if end < 0 or data[start:end].decode("ascii") != value["text"]:
                raise ValueError(f"{name} original string mismatch")
            # Check the full terminated string remains inside one mapped region.
            mapped_offset(image, pointer, end - start + 1)
            identifier = value["identifier"]
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", identifier) or identifier in identifiers:
                raise ValueError(f"{name} invalid or duplicate identifier")
            identifiers.add(identifier)
    return catalog


def outputs(catalog: dict) -> dict[str, str]:
    notice = (
        "// Reconstructed from PicoBrew Z 0.0.119 lookup tables and bounds.\n"
        "// Original names/values: analysis/enums.json. This is not the lost vendor source.\n"
        "// No original image or absolute application RAM is used at runtime.\n"
    )
    header = [notice, "#pragma once\n#include <cstddef>\n#include <cstdint>\n\nnamespace zseries { namespace oem {\n"]
    source = [notice, '#include "zseries/enums.hpp"\n\nnamespace zseries { namespace oem {\n']
    for enum in catalog["enums"]:
        name = enum["name"]
        header += [f"\n// DROM 0x{enum['table_address']:08x}; observed bound in 0x{enum['evidence_function']:08x}.\n",
                   f"enum class {name} : std::uint32_t {{\n"]
        for value in enum["values"]:
            note = " // Identifier normalized from the display label." if value["identifier_from_display_label"] else ""
            header.append(f"    {value['identifier']} = {value['value']},{note}\n")
        header += ["};\n", f"constexpr std::size_t {name}Count = {enum['count']};\n",
                   f"const char* to_string({name} value) noexcept;\n"]
        source.append(f"\nconst char* to_string({name} value) noexcept {{\n    static const char* const names[] = {{\n")
        for value in enum["values"]:
            source.append("        " + json.dumps(value["text"]) + ",\n")
        source += ["    };\n", "    const auto index = static_cast<std::uint32_t>(value);\n",
                   f'    return index < {name}Count ? names[index] : "<{name}::INVALID>";\n', "}\n"]
    header.append("\n}} // namespace zseries::oem\n")
    source.append("\n}} // namespace zseries::oem\n")
    return {"include/zseries/enums.hpp": "".join(header), "src/enums.cpp": "".join(source)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalog = validated_catalog()
    for name, content in outputs(catalog).items():
        path = ROOT / name
        if args.check:
            if not path.is_file() or path.read_text() != content:
                raise SystemExit(f"reconstructed source is stale: {name}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    print(f"Verified {len(catalog['enums'])} enum tables and {sum(x['count'] for x in catalog['enums'])} original numeric labels")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
