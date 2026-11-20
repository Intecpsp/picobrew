#!/usr/bin/env python3
"""Recover all statically resolved Z119 programs as editable typed C++ tables."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import analyze_images
from recover_enums import mapped_offset

ROOT = Path(__file__).resolve().parents[1]
IMAGE = "zseries_0_0_119.bin"
# Branch destinations reviewed in original resolver 0x400d7df8. The regression
# harness independently executes that resolver for every ID and compares them.
PROGRAMS = {
    1: 0x3f428b7c, 2: 0x3f428b60, 3: 0x3f428b44, 4: 0x3f428b28,
    5: 0x3f428ad4, 6: 0x3f428ab8, 7: 0x3f428a9c, 8: 0x3f428a64,
    9: 0x3f428a48, 10: 0x3f428a2c, 11: 0x3f428a10, 12: 0x3f4289d8,
    13: 0x3f4289bc, 14: 0x3f4289a0, 15: 0x3f428984, 16: 0x3f428968,
    17: 0x3f42894c, 18: 0x3f4289f4, 19: 0x3f428930, 25: 0x3f428914,
    26: 0x3f428a80, 27: 0x3f428b0c, 28: 0x3f428af0,
}


def recover():
    raw = (ROOT.parent / "oem/upstream" / IMAGE).read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != analyze_images.IMAGES[IMAGE]:
        raise ValueError("Preserved original image identity differs")
    image = analyze_images.parse_image(raw)
    enums = {e["name"]: {v["value"]: v["identifier"] for v in e["values"]}
             for e in json.loads((ROOT / "analysis/enums.json").read_text())["enums"]}
    def read(address, size):
        start = mapped_offset(image, address, size)
        return raw[start:start + size]
    def string(address):
        start = mapped_offset(image, address, 1)
        end = raw.find(b"\0", start, start + 512)
        if end < 0: raise ValueError("Unterminated program string")
        value = read(address, end - start + 1)
        return dict(address=address, text=value[:-1].decode("ascii"), sha256=hashlib.sha256(value).hexdigest())
    programs = []
    for program_id, address in PROGRAMS.items():
        header = read(address, 28)
        kind, unique_id, name, strings, strings_count, code, code_count = struct.unpack("<7I", header)
        if kind not in enums["SessionType"] or not 0 < code_count <= 256 or strings_count > 64:
            raise ValueError("Unreviewed program record bounds")
        code_bytes = read(code, code_count * 8)
        instructions = []
        for index, (opcode, argument) in enumerate(struct.iter_unpack("<II", code_bytes)):
            if opcode not in enums["OpCode"]: raise ValueError("Unrecognized opcode")
            instructions.append(dict(address=code + 8 * index, opcode=opcode,
                                     name=enums["OpCode"][opcode], argument=argument))
        pointers = read(strings, strings_count * 4) if strings_count else b""
        if bool(strings_count) != bool(strings): raise ValueError("Inconsistent original string table")
        string_table = [string(pointer) for pointer, in struct.iter_unpack("<I", pointers)]
        programs.append(dict(id=program_id, identifier=enums["ZProgramId"][program_id], address=address,
            header_sha256=hashlib.sha256(header).hexdigest(), session_type=kind,
            session_identifier=enums["SessionType"][kind], unique_id=unique_id,
            name=string(name), code_address=code, code_sha256=hashlib.sha256(code_bytes).hexdigest(),
            instructions=instructions, strings_address=strings, strings_sha256=hashlib.sha256(pointers).hexdigest(),
            strings=string_table))
    return dict(schema=1, input=IMAGE, input_sha256=digest, resolver_entry=0x400d7df8,
                buffered_ids=list(range(20, 25)), programs=programs)


def source(catalog):
    out = ['// Recovered original Z119 programs, not the lost vendor source.\n',
           '// Source addresses and byte hashes: analysis/programs.json.\n',
           '// No original image, absolute addresses, or decompiler text used at runtime.\n',
           '#include "zseries/program_repository.hpp"\n\nnamespace zseries { namespace oem {\nnamespace {\n']
    for program in catalog["programs"]:
        name = program["identifier"]
        out.append(f"// {program['name']['text']}, original record 0x{program['address']:08x}.\n")
        out.append(f"const Instruction {name}Code[] = {{\n")
        for instruction in program["instructions"]:
            out.append(f"    {{OpCode::{instruction['name']}, 0x{instruction['argument']:x}u}}, // 0x{instruction['address']:08x}\n")
        out.append("};\n")
        if program["strings"]:
            out.append(f"const char* const {name}Strings[] = {{\n")
            for item in program["strings"]:
                out.append("    " + json.dumps(item["text"]) + f", // 0x{item['address']:08x}\n")
            out.append("};\n")
        out.append("\n")
    out += ["} // namespace\n\nconst std::array<ProgramRecord, 23>& builtin_programs() noexcept {\n",
            "    static const std::array<ProgramRecord, 23> programs = {{\n"]
    for program in catalog["programs"]:
        name = program["identifier"]
        strings = name + "Strings" if program["strings"] else "nullptr"
        out.append(f"        ProgramRecord(SessionType::{program['session_identifier']}, 0x{program['unique_id']:x}u, " +
                   json.dumps(program["name"]["text"]) + f", {name}Code, {len(program['instructions'])}, {strings}, {len(program['strings'])}),\n")
    out += ["    }};\n    return programs;\n}\n\nconst ProgramRecord* builtin_program(std::uint32_t id) noexcept {\n",
            "    switch (id) {\n"]
    for index, program in enumerate(catalog["programs"]):
        out.append(f"        case {program['id']}: return &builtin_programs()[{index}];\n")
    out += ["        default: return nullptr;\n    }\n}\n}} // namespace zseries::oem\n"]
    return "".join(out)


def outputs():
    catalog = recover()
    return {"analysis/programs.json": json.dumps(catalog, indent=2) + "\n",
            "src/program_catalog.cpp": source(catalog)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    for name, content in outputs().items():
        path = ROOT / name
        if options.check:
            if not path.is_file() or path.read_text() != content: raise SystemExit(f"Stale program recovery: {name}")
        else:
            path.write_text(content)
    print("Verified all 23 original built-in programs and their typed instruction/string source")


if __name__ == "__main__":
    main()
