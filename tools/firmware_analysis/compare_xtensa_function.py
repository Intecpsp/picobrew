#!/usr/bin/env python3
"""Compare an ELF function with a linked image, accounting for Xtensa relaxation.

Inputs are instruction exports from ExportFirmware.java, the reference ELF object,
and the original image/export manifest. A successful comparison identifies an SDK
candidate; it does not execute either function or prove external callees equivalent.
Requires pyelftools. No input bytes or Ghidra projects are modified.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalized(instructions):
    """Keep registers, immediates and branch destinations; normalize call encoding."""
    result = []
    i = 0
    while i < len(instructions):
        ins = instructions[i]
        mnemonic = ins["mnemonic"].removesuffix(".n")
        operands = list(ins["operands"])
        addresses = [ins["address"]]
        pair = None
        if (mnemonic == "l32r" and i + 1 < len(instructions)
                and instructions[i + 1]["mnemonic"] == "callx8"
                and operands[0] == instructions[i + 1]["operands"][0]
                and int(instructions[i + 1]["address"], 16)
                    == int(ins["address"], 16) + len(bytes.fromhex(ins["bytes"]))):
            pair = instructions[i + 1]
            addresses.append(pair["address"])
            mnemonic, operands = "call8", ["<call>"]
            i += 1
        elif mnemonic == "call8":
            operands = ["<call>"]
        elif mnemonic == "l32r":
            operands = [operands[0], "<literal>"]
        result.append({"addresses": addresses, "mnemonic": mnemonic,
                       "operands": operands, "instruction": ins, "pair": pair})
        i += 1
    indices = {address: n for n, item in enumerate(result) for address in item["addresses"]}
    for item in result:
        if item["mnemonic"] not in ("call8", "callx8"):
            flows = item["instruction"].get("flows", [])
            item["operands"] = ["instruction:" + str(indices[x]) if x in flows and x in indices else x
                                for x in item["operands"]]
    return result


def shape(items):
    return [(item["mnemonic"], item["operands"]) for item in items]


def compare(args):
    image = args.image.read_bytes()
    manifest = json.loads(args.manifest.read_text())
    if digest(image) != manifest["input"]["sha256"]:
        raise ValueError("Image SHA256 does not match the export manifest")

    def read_image(address, size):
        for segment in manifest["layout"]["segments"]:
            delta = address - segment["address"]
            if 0 <= delta and delta + size <= segment["size"]:
                start = segment["offset"] + delta
                value = image[start:start + size]
                if len(value) == size:
                    return value
        raise ValueError(f"Address range not present in image: {address:#x}+{size}")

    obj = args.object.read_bytes()
    elf = ELFFile(io.BytesIO(obj))
    if elf["e_machine"] != "EM_XTENSA" or not elf.little_endian:
        raise ValueError("Expected a little-endian Xtensa ELF object")
    symbols = elf.get_section_by_name(".symtab")
    functions = [s for s in symbols.iter_symbols() if s.name == args.symbol
                 and isinstance(s["st_shndx"], int) and s["st_info"]["type"] == "STT_FUNC"]
    if len(functions) != 1:
        raise ValueError("Reference function must have one defined symbol")
    function = functions[0]
    section_index = function["st_shndx"]
    section = elf.get_section(section_index)
    relocations = {}
    for reloc_section in elf.iter_sections():
        if reloc_section["sh_type"] not in ("SHT_RELA", "SHT_REL"):
            continue
        table = elf.get_section(reloc_section["sh_link"])
        for r in reloc_section.iter_relocations():
            key = (reloc_section["sh_info"], r["r_offset"], r["r_info_type"])
            relocations[key] = (table.get_symbol(r["r_info_sym"]), r.entry.get("r_addend", 0))

    def reference_relocation(ins, kind):
        offset = int(ins["address"], 16) - args.reference_base + function["st_value"]
        key = (section_index, offset, kind)
        if key not in relocations:
            raise ValueError(f"Missing reference relocation {key}")
        return relocations[key]

    reference_bytes, original_bytes = args.reference.read_bytes(), args.original.read_bytes()
    reference, original = json.loads(reference_bytes), json.loads(original_bytes)
    for instructions in (reference, original):
        if not instructions:
            raise ValueError("Empty instruction export")
        previous_end = -1
        for ins in instructions:
            address = int(ins["address"], 16)
            data = bytes.fromhex(ins["bytes"])
            if not data or address < previous_end:
                raise ValueError("Empty, overlapping or out-of-order instruction bytes")
            previous_end = address + len(data)
    # Bind every decoded instruction back to its exact input bytes before comparing.
    for ins in reference:
        offset = int(ins["address"], 16) - args.reference_base + function["st_value"]
        data = bytes.fromhex(ins["bytes"])
        if not function["st_value"] <= offset < offset + len(data) <= function["st_value"] + function["st_size"]:
            raise ValueError("Reference instruction lies outside the declared function")
        if section.data()[offset:offset + len(data)] != data:
            raise ValueError("Reference instructions do not match ELF bytes")
    for ins in original:
        data = bytes.fromhex(ins["bytes"])
        if read_image(int(ins["address"], 16), len(data)) != data:
            raise ValueError("Original instructions do not match image bytes")
    left, right = normalized(reference), normalized(original)
    if shape(left) != shape(right):
        raise ValueError("Instruction, register, immediate or branch structure differs")

    bindings, calls, literals = {}, [], []

    def bind(symbol, addend, address):
        name = symbol.name or elf.get_section(symbol["st_shndx"]).name
        key = f"{name}{addend:+#x}" if addend else name
        if key in bindings and bindings[key] != address:
            raise ValueError(f"Inconsistent external binding for {key}")
        bindings[key] = address
        return key

    for ref, actual in zip(left, right):
        if ref["mnemonic"] == "call8":
            symbol, addend = reference_relocation(ref["instruction"], 11)
            actual_call = actual["pair"] or actual["instruction"]
            if actual["pair"]:
                pointer = int(actual["instruction"]["operands"][1], 16)
                target = int.from_bytes(read_image(pointer, 4), "little")
                if actual_call.get("flows") and actual_call["flows"] != [f"0x{target:08x}"]:
                    raise ValueError("Computed call target conflicts with image literal")
            else:
                target = int(actual_call["operands"][0], 16)
            key = bind(symbol, addend, target)
            calls.append({"symbol": key, "address": f"0x{target:08x}"})
        elif ref["mnemonic"] == "l32r":
            literal_symbol, addend = reference_relocation(ref["instruction"], 20)
            literal_index = literal_symbol["st_shndx"]
            literal_offset = literal_symbol["st_value"] + addend
            literal_section = elf.get_section(literal_index)
            reference_value = literal_section.data()[literal_offset:literal_offset + 4]
            if len(reference_value) != 4:
                raise ValueError("Reference literal is truncated")
            address = int(actual["instruction"]["operands"][1], 16)
            actual_value = read_image(address, 4)
            relocated = relocations.get((literal_index, literal_offset, 1))
            if relocated:
                symbol, offset = relocated
                # Xtensa R_XTENSA_32 retains an in-place addend in the word.
                offset += int.from_bytes(reference_value, "little")
                target = int.from_bytes(actual_value, "little")
                key = bind(symbol, offset, target)
                entry = {"reference": key, "value": f"0x{target:08x}"}
                if isinstance(symbol["st_shndx"], int):
                    data_section = elf.get_section(symbol["st_shndx"])
                    if data_section.name.startswith(".rodata.str"):
                        string = data_section.data()[symbol["st_value"] + offset:].split(b"\0", 1)[0] + b"\0"
                        if read_image(target, len(string)) != string:
                            raise ValueError("Referenced string bytes differ")
                        entry["string_hex"] = string.hex()
            else:
                if reference_value != actual_value:
                    raise ValueError("Unrelocated literal value differs")
                entry = {"constant": f"0x{int.from_bytes(actual_value, 'little'):08x}"}
            literals.append(entry)

    return {
        "schema_version": 1, "method": "Xtensa instruction structure with call/density relaxation; exact literals and repeated external bindings",
        "symbol": args.symbol, "image_sha256": digest(image), "object_sha256": digest(obj),
        "reference_export_sha256": digest(reference_bytes), "original_export_sha256": digest(original_bytes),
        "reference_address": f"0x{args.reference_base:08x}", "original_address": original[0]["address"],
        "reference_instruction_count": len(reference), "original_instruction_count": len(original),
        "normalized_instruction_count": len(left), "instruction_structure_matches": True,
        "literal_checks": literals, "call_bindings": calls,
        "external_bindings": {key: f"0x{value:08x}" for key, value in sorted(bindings.items())},
        "external_callee_equivalence_proven": False, "behaviorally_verified": False, "hardware_tested": False,
        "limitations": ["Relocated external pointers are inferred correspondences, not independent proof of their targets.",
                        "Ghidra instruction coverage and function boundaries remain analysis inputs.",
                        "No emulation, whole-library equivalence, source provenance or hardware compatibility is claimed."],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("image", "manifest", "object", "reference", "original", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--reference-base", type=lambda x: int(x, 0), required=True)
    args = parser.parse_args()
    result = compare(args)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Matched {result['normalized_instruction_count']} normalized instructions for {args.symbol}; "
          f"{len(result['literal_checks'])} literal checks, {len(result['call_bindings'])} calls")


if __name__ == "__main__":
    main()
