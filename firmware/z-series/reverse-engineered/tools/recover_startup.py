#!/usr/bin/env python3
"""Record verified Z119 startup bytes, call bindings, and component boundaries."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

import analyze_images

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = {
    0x400ee978: "application_entry",
    0x4012186c: "core_bootstrap",
    0x401218d4: "core_task_loop",
    0x40120a04: "core_constructor",
    0x40113078: "core_setup",
    0x40120120: "core_tick",
    0x40134560: "run_static_initializers",
    0x40110aa8: "system_constructor",
    0x40112f88: "system_setup",
    0x40125ae0: "wire_static_initializer",
    0x400d2160: "enable_heater_timers",
    0x400d5b24: "screen_power",
}
LITERALS = {
    0x4012b464: (0x3f4858cc, "last_static_initializer_pointer"),
    0x4012b470: (0x3f48581c, "first_static_initializer_pointer"),
    0x40121860: (0x15ab8, "core_allocation_bytes"),
    0x40121864: (0x3ffc2e88, "core_global_pointer"),
    0x401218d0: (0x40081374, "yield_callback"),
    0x400f3d34: (0x40081388, "millis_callback"),
    0x40121858: (0x0800001c, "serial_configuration"),
    0x4012185c: (115200, "serial_baud"),
    0x40113068: (20000, "serial_timeout_ms_and_fast_timer_us"),
    0x40113020: (0x3ffc3610, "serial_object"),
    0x40112f80: (4000000, "spi_frequency_hz"),
    0x40112f84: (400000, "i2c_frequency_hz"),
    0x40113054: (0x40080570, "buzzer_timer_callback"),
    0x4011305c: (0x400805dc, "slow_heater_timer_callback"),
    0x40113060: (0x40080540, "fast_heater_timer_callback"),
    0x40113064: (1000000, "slow_heater_timer_us"),
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "analysis/startup-bindings.json")
    args = parser.parse_args()
    original = ROOT.parent / "oem/upstream/zseries_0_0_119.bin"
    data = original.read_bytes()
    expected_sha = analyze_images.IMAGES[original.name]
    if hashlib.sha256(data).hexdigest() != expected_sha:
        raise SystemExit("Original image identity mismatch")
    segments = analyze_images.parse_image(data)["segments"]

    def read(address, size):
        for segment in segments:
            if segment["load_address"] <= address and address + size <= segment["load_address"] + segment["size_bytes"]:
                offset = segment["file_offset"] + address - segment["load_address"]
                return offset, data[offset:offset + size]
        raise ValueError(f"Unmapped range {address:#x}+{size}")

    bindings = []
    for address, (value, label) in LITERALS.items():
        offset, raw = read(address, 4)
        if struct.unpack("<I", raw)[0] != value:
            raise SystemExit(f"Original literal mismatch: {label}")
        bindings.append({"name": label, "address": f"0x{address:08x}", "file_offset": offset,
                         "value": value, "bytes": raw.hex()})
    inventory = {int(item["address"], 16): item for item in json.loads((args.export / "functions.json").read_text())}
    functions = []
    for address, label in FUNCTIONS.items():
        path = args.export / "instructions" / f"fn_{address:08x}.json"
        instructions = json.loads(path.read_text())
        for instruction in instructions:
            raw = bytes.fromhex(instruction["bytes"])
            if read(int(instruction["address"], 16), len(raw))[1] != raw:
                raise SystemExit("Instruction export does not match original bytes")
        functions.append({"address": f"0x{address:08x}", "recovered_role": label,
                          "instruction_count": len(instructions), "instruction_export_sha256": digest(path),
                          "original_instruction_bytes_sha256": hashlib.sha256(b"".join(bytes.fromhex(i["bytes"]) for i in instructions)).hexdigest(),
                          "direct_call_targets": sorted({r["target"] for i in instructions for r in i["references"]
                                                         if r["type"] == "UNCONDITIONAL_CALL"})})
    initializers = []
    for address in range(0x3f48581c, 0x3f4858d0, 4):
        offset, raw = read(address, 4)
        target = struct.unpack("<I", raw)[0]
        if target not in inventory:
            raise SystemExit(f"Static initializer has no reviewed entry: {target:#x}")
        initializers.append({"pointer_address": f"0x{address:08x}", "file_offset": offset,
                             "target": f"0x{target:08x}", "discovered_function_name": inventory[target]["name"]})
    vtable = []
    for offset in range(0, 28, 4):
        raw = read(0x3f41feb4 + offset, 4)[1]
        vtable.append({"offset": offset, "target": f"0x{struct.unpack('<I', raw)[0]:08x}"})
    candidate_report = json.loads((ROOT / "analysis/callback-entry-candidates.json").read_text())
    discovery_report = json.loads((ROOT / "analysis/callback-discovery.json").read_text())
    summary = json.loads((args.export / "ghidra-summary.json").read_text())
    report = {
        "schema_version": 1, "input_sha256": expected_sha,
        "scope": "Original bytes, static initialization table, instruction export identities, and manually traced application bindings; not a complete linked application",
        "literals": bindings, "functions": functions,
        "static_initializers": {"first": "0x3f48581c", "last": "0x3f4858cc", "count": len(initializers),
                                "call_order": "descending pointer address", "runner": "0x40134560", "entries": initializers},
        "core_vtable": {"address": "0x3f41feb4", "entries": vtable, "setup_offset": 12, "tick_offset": 16},
        "task": {"entry": "0x401218d4", "name": "coreLoopTask", "stack_bytes": 16384, "priority": 1, "core_id": 1,
                 "created_by": "0x400ee978", "task_creation_function": "0x40091254"},
        "hardware_bindings": {
            "spi": {"bus": 2, "sck": 17, "miso": 5, "mosi": 16, "ss": -1, "frequency_hz": 4000000, "data_mode": 0},
            "i2c": [{"bus": 0, "sdk_object": "0x3ffc3158", "sda": 22, "scl": 23, "frequency_hz": 400000,
                     "expected_device_addresses": [0x76, 0x19, 0x34]},
                    {"bus": 1, "sdk_object": "0x3ffc3028", "sda": 25, "scl": 26, "frequency_hz": 400000,
                     "expected_device_addresses": [0x40]}],
            "screen_power_pin": 33, "heater_output_pin": 12, "heater_enable_pin": 19,
            "hardware_timers": [{"id": 0, "divider": 80, "count_up": True, "edge": True, "period_us": 1000000,
                                 "callback": "0x400805dc", "auto_reload": True},
                                {"id": 1, "divider": 80, "count_up": True, "edge": True, "period_us": 20000,
                                 "callback": "0x40080540", "auto_reload": True}],
            "task_watchdog": {"timeout_seconds": 45, "panic": True, "register_current_task": True},
            "claim": "Observed 0.0.119 application configuration, not independent physical board identification"},
        "discovery": {key: candidate_report[key] for key in ("baseline_recognized_functions", "aligned_entry_candidates",
                      "new_entry_candidates", "new_address_taken_candidates", "new_unreferenced_candidates")},
        "discovery_definition_counts": {key: discovery_report[key] for key in ("new_functions_defined", "overlapping_candidates", "rejected_candidates")},
        "expanded_export_summary": summary,
        "source_sha256": {path: digest(ROOT / path) for path in ("tools/recover_startup.py", "tools/discover_function_entries.py",
                          "tools/ghidra/DiscoverZCallbacks.java", "analysis/callback-entry-candidates.json", "analysis/callback-discovery.json")},
        "expanded_function_inventory_sha256": digest(args.export / "functions.json"),
        "function_inventory_exhaustive": False, "hardware_tested": False,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"static_initializers": len(initializers), "startup_functions": len(functions),
                      "verified_literals": len(bindings), "function_inventory_exhaustive": False}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
