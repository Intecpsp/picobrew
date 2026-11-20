#!/usr/bin/env python3
"""Find address-taken Xtensa ENTRY candidates omitted by initial analysis.

This is a discovery catalog, not a complete function list or proof of source
behavior. Unreferenced aligned prologues remain candidates for manual review.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

import analyze_images

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = (ROOT.parent / "oem/upstream/zseries_0_0_119.bin").read_bytes()
    expected = analyze_images.IMAGES["zseries_0_0_119.bin"]
    if hashlib.sha256(data).hexdigest() != expected:
        raise SystemExit("Original image identity mismatch")
    image = analyze_images.parse_image(data)
    functions_path = args.export / "functions.json"
    functions = json.loads(functions_path.read_text())
    known = {int(item["address"], 16) for item in functions}
    candidates = {}
    for segment in image["segments"]:
        if segment["memory_kind"] not in ("IROM", "IRAM"):
            continue
        for offset in range(0, segment["size_bytes"] - 2, 4):
            address, position = segment["load_address"] + offset, segment["file_offset"] + offset
            instruction = int.from_bytes(data[position:position + 3], "little")
            stack_bytes = (instruction >> 12) << 3
            # Xtensa little-endian ENTRY as,imm12: opcode 0x36; as=a1.
            # Restrict supplementary candidates to the compiler's observed
            # 4-byte entry and16-byte frame alignment. This is not exhaustive.
            if data[position] == 0x36 and data[position + 1] & 15 == 1 and stack_bytes >= 16 and stack_bytes % 16 == 0:
                candidates[address] = {"address": f"0x{address:08x}", "file_offset": position,
                                       "stack_frame_bytes": stack_bytes, "pointer_occurrences": []}
    for segment in image["segments"]:
        for offset in range(0, segment["size_bytes"] - 3, 4):
            position = segment["file_offset"] + offset
            value = struct.unpack_from("<I", data, position)[0]
            if value in candidates:
                candidates[value]["pointer_occurrences"].append({
                    "address": f"0x{segment['load_address'] + offset:08x}", "file_offset": position,
                    "memory_kind": segment["memory_kind"],
                })
    new = []
    for address, item in sorted(candidates.items()):
        if address in known:
            continue
        item["seed_from_pointer_evidence"] = bool(item["pointer_occurrences"])
        new.append(item)
    result = {
        "schema_version": 1, "input_sha256": expected,
        "baseline_function_inventory_sha256": hashlib.sha256(functions_path.read_bytes()).hexdigest(),
        "baseline_recognized_functions": len(functions), "aligned_entry_candidates": len(candidates),
        "already_recognized_entries": sum(address in known for address in candidates),
        "new_entry_candidates": len(new),
        "new_address_taken_candidates": sum(item["seed_from_pointer_evidence"] for item in new),
        "new_unreferenced_candidates": sum(not item["seed_from_pointer_evidence"] for item in new),
        "candidate_filter": "Mapped executable bytes; four-byte aligned ENTRY a1; frame at least16 and multiple of16; original aligned pointer values recorded",
        "scope": "Candidate discovery; raw pointer occurrences do not themselves prove a callable function or a constructor table",
        "function_inventory_exhaustive": False, "behaviorally_verified": False, "hardware_tested": False,
        "new_candidates": new,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("aligned_entry_candidates", "new_entry_candidates", "new_address_taken_candidates", "new_unreferenced_candidates")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
