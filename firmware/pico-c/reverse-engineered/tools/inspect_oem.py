#!/usr/bin/env python3
"""Verify/decode preserved Pico C envelopes and reproduce analysis anchors.

This is offline analysis. It never flashes hardware or changes OEM files.
Exact spans are anchors, not a claim that every matched call or peripheral has
the same semantics: a span ends at the next entry in the S/Pro recovery map.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
FAMILY = ROOT.parent
REPO = FAMILY.parents[1]
IMAGE_BASE = 0x20004000
VARIANTS = {
    "ssd1325": {"file": "pico_c_0_1_34.bin", "envelope_sha256": "10d508bd1f45aece1b016907478d9dd222809835af533c9577aeb337232d36e8",
                "raw_sha256": "ee732d03a1869985f9353e4208073ebfd1182df11d6d9f0dbf834ee76fcdd45b", "display": "SSD1325", "build_time": "12:59:11"},
    "ssd1309": {"file": "pico_c_0_1_34_alt.bin", "envelope_sha256": "004bd841e2b8919acbad45d9837c3547d218a3dcbd0767d24df3c6827f323225",
                "raw_sha256": "6f2dedfcd7ab10b5a421b05dc4ee792cd44753d7e302fd8ee645ca4f6556c226", "display": "SSD1309", "build_time": "13:00:10"},
}
ENCODER_ROUTINES = [
    ("construct", 0x20004574, 18), ("set_step", 0x20004586, 4),
    ("set_range", 0x2000458A, 12), ("enable_wrap", 0x20004596, 8),
    ("disable_wrap", 0x2000459E, 8), ("set_position", 0x200045A6, 8),
    ("get_position", 0x200045AE, 52), ("take_click", 0x200045E2, 16),
    ("clear_click", 0x200045F2, 6), ("attach", 0x200045F8, 64),
    ("initialize", 0x20004638, 120), ("handle_rotation", 0x200046B0, 90),
]
HELPERS = {
    "ssd1325": {"digital_read": 0x20017F10, "pin_mode": 0x20017E98,
                "interrupts_disable": 0x20018922, "interrupts_enable": 0x20018918,
                "attach_interrupt": 0x20017C40, "rotation_handler": 0x2000470D,
                "button_handler": 0x20004739},
    "ssd1309": {"digital_read": 0x20017E6C, "pin_mode": 0x20017DF4,
                "interrupts_disable": 0x2001887E, "interrupts_enable": 0x20018874,
                "attach_interrupt": 0x20017B9C, "rotation_handler": 0x20004711,
                "button_handler": 0x2000473D},
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def decode_envelope(envelope):
    match = re.fullmatch(rb"#([1-9][0-9]*)(?:\.0)?,([0-9a-f]{32})\|([0-9a-f]+)#", envelope)
    if not match or len(match[3]) % 2:
        raise ValueError("Malformed Pico update envelope")
    raw = bytes.fromhex(match[3].decode("ascii"))
    if len(raw) != int(match[1]):
        raise ValueError("Envelope length mismatch")
    nibble_sum = sum((-1 if index % 15 == 0 else 1) * int(chr(character), 16)
                     for index, character in enumerate(match[3])) & 0xFFFFFFFF
    signed = nibble_sum if nibble_sum < 0x80000000 else nibble_sum - 0x100000000
    checksum = hashlib.md5(str(signed).encode("ascii")).hexdigest()
    if checksum != match[2].decode("ascii"):
        raise ValueError("Legacy transport checksum mismatch")
    return raw


def read_variant(variant):
    definition = VARIANTS[variant]
    envelope = (FAMILY / "oem/upstream" / definition["file"]).read_bytes()
    if digest(envelope) != definition["envelope_sha256"]:
        raise ValueError("Preserved OEM envelope changed")
    raw = decode_envelope(envelope)
    if digest(raw) != definition["raw_sha256"]:
        raise ValueError("Decoded OEM image changed")
    sp, reset = struct.unpack_from("<II", raw)
    if sp & 3 or not 0x20000000 <= sp <= 0x20040000 or not reset & 1 or not IMAGE_BASE <= reset & ~1 < IMAGE_BASE + len(raw):
        raise ValueError("Implausible Cortex-M vectors")
    return raw


def cstring(raw, offset):
    end = raw.index(0, offset)
    return raw[offset:end].decode("ascii")


def field_after(raw, name):
    offset = raw.index(name.encode("ascii") + b"\0") + len(name) + 1
    return cstring(raw, offset)


def shared_anchors(raw):
    reference = REPO / "firmware/pico-s-pro/reverse-engineered/reference"
    original = (reference / "original-0.1.34.bin").read_bytes()
    functions = sorted(json.loads((reference / "function_map.json").read_text()), key=lambda f: int(f["address"], 16))
    anchors = []
    for current, following in zip(functions, functions[1:]):
        start, end = int(current["address"], 16), int(following["address"], 16)
        span = original[start - IMAGE_BASE:end - IMAGE_BASE]
        if len(span) < 8 or len(span) > 16384:
            continue
        offset = raw.find(span)
        if offset < 0 or offset & 1 or raw.find(span, offset + 1) >= 0:
            continue
        anchors.append({"name": current["name"], "module": current["module"],
                        "address": f"0x{IMAGE_BASE + offset:08x}",
                        "s_pro_address": f"0x{start:08x}", "span_bytes": len(span),
                        "span_sha256": digest(span)})
    return anchors


def reports():
    inventory = {"schema_version": 1, "source_repository": "https://github.com/chiefwigms/picobrew_pico",
                 "source_commit": "36acae3003845b72568ef5f7d6cf168acb6f9b69",
                 "load_address": f"0x{IMAGE_BASE:08x}", "architecture": "ARM Cortex-M, little-endian Thumb",
                 "hardware_tested": False, "full_source_reconstruction": False,
                 "checksum_note": "OEM MD5 is over a signed nibble sum, not over raw image bytes; it is not a signature.",
                 "variants": {}}
    anchors = {"schema_version": 1,
               "method": "One unique, halfword-aligned byte-for-byte match of each S/Pro interval between adjacent known function starts, minimum 8 bytes.",
               "limitations": ["Intervals may include literal pools or alignment padding.",
                               "Identical PC-relative instruction bytes do not independently establish callee semantics.",
                               "Unmatched routines are not reconstructed; absence of a match does not prove absence of the feature.",
                               "Display controller variant and physical hardware identity are distinct from byte-span similarity."],
               "variants": {}}
    routines = {"schema_version": 1,
                "status": "Partial typed reconstruction; not a full firmware image or model port.",
                "validation": "Native source is compared with original ARM instructions for return values, object bytes, and observed GPIO/interrupt call order.",
                "variants": {}}
    for variant, definition in VARIANTS.items():
        raw = read_variant(variant)
        strings = [(IMAGE_BASE + m.start(), m.group().decode("ascii")) for m in re.finditer(rb"[ -~]{6,}", raw)]
        fields = {"model": "PICO_C", "version": "v0.1.34", "build_date": "Dec 16 2019",
                  "build_time": definition["build_time"], "git_hash": field_after(raw, "GIT_HASH"),
                  "git_branch": field_after(raw, "GIT_BRANCH"), "display_controller": field_after(raw, "SSD_TYPE")}
        for value in fields.values():
            if value.encode("ascii") + b"\0" not in raw:
                raise ValueError("Missing expected diagnostic identity")
        if fields["display_controller"] != definition["display"]:
            raise ValueError("Display variant mismatch")
        matched = shared_anchors(raw)
        inventory["variants"][variant] = {"oem_file": "oem/upstream/" + definition["file"],
            "envelope_sha256": definition["envelope_sha256"], "payload_sha256": digest(raw), "payload_bytes": len(raw),
            "initial_stack_pointer": f"0x{struct.unpack_from('<I', raw)[0]:08x}",
            "reset_vector": f"0x{struct.unpack_from('<I', raw, 4)[0]:08x}", "embedded_identity": fields,
            "identity_string_addresses": {value: f"0x{IMAGE_BASE + raw.index(value.encode('ascii') + bytes([0])):08x}" for value in fields.values()},
            "printable_string_count_minimum_length_6": len(strings), "unique_s_pro_span_anchors": len(matched)}
        anchors["variants"][variant] = matched
        entries = []
        for name, address, size in ENCODER_ROUTINES:
            address += 4 if variant == "ssd1309" else 0
            entries.append({"name": "pico_c_encoder_" + name, "source": "src/encoder.c",
                            "address": f"0x{address:08x}", "span_bytes": size,
                            "span_sha256": digest(raw[address - IMAGE_BASE:address - IMAGE_BASE + size])})
        address = 0x200069E0 if variant == "ssd1325" else 0x200069C8
        entries.append({"name": "pico_c_parse_ipv4_address", "source": "src/network_parse.c",
                        "address": f"0x{address:08x}", "span_bytes": 74,
                        "span_sha256": digest(raw[address - IMAGE_BASE:address - IMAGE_BASE + 74])})
        routines["variants"][variant] = {"routines": entries,
            "hooked_hardware_boundaries": {key: f"0x{value:08x}" for key, value in HELPERS[variant].items()}}
    return {"inventory.json": inventory, "shared-function-anchors.json": anchors,
            "routines.json": routines}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify committed analysis without rewriting it")
    parser.add_argument("--extract", action="store_true", help="Write verified raw payloads into ignored build/ for disassembly")
    args = parser.parse_args()
    for name, report in reports().items():
        path = ROOT / "analysis" / name
        data = (json.dumps(report, indent=2, ensure_ascii=True) + "\n").encode("ascii")
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit(f"Stale or missing analysis: {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    if args.extract:
        (ROOT / "build").mkdir(exist_ok=True)
        for variant, definition in VARIANTS.items():
            (ROOT / "build" / definition["file"]).write_bytes(read_variant(variant))
    print("Pico C: both preserved envelopes, transport checksums, raw hashes and analysis verified")


if __name__ == "__main__":
    main()
