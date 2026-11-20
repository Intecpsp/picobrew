#!/usr/bin/env python3
"""Reproduce original instruction argument checks and numeric decoding."""
from __future__ import annotations
import argparse
import collections
import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INPUT_SHA256 = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"


def input_cases():
    arguments = set(range(257)) | {25087, 25088, 25089, 0xffffffff, 0x40000000,
                                  0x70000000, 0x80000000, 0xf0000000, 0xf0000007, 0xf0000008}
    for bit in range(32):
        for delta in (-1, 0, 1):
            arguments.add(((1 << bit) + delta) & 0xffffffff)
    generator = random.Random(119)
    arguments.update(generator.getrandbits(32) for _ in range(128))
    opcodes = list(range(60)) + [255, 0x7fffffff, 0x80000000, 0xffffffff]
    return [(opcode, arg) for opcode in opcodes for arg in sorted(arguments)]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-input", type=Path)
    parser.add_argument("--original-csv", type=Path)
    parser.add_argument("--export", type=Path)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    cases = input_cases()
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True, exist_ok=True)
        with args.prepare_input.open("w") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(["opcode", "argument"])
            writer.writerows(cases)
        print(f"Prepared {len(cases)} instruction cases")
    if not args.original_csv:
        if not args.prepare_input:
            parser.error("use --prepare-input or --original-csv")
        return 0
    if not args.export:
        parser.error("--original-csv requires --export")
    if digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != INPUT_SHA256:
        raise SystemExit("Original image identity mismatch")
    execution = json.loads(Path(str(args.original_csv) + ".json").read_text())
    if (execution["input_sha256"] != INPUT_SHA256 or execution["validation_cases"] != len(cases) or
            execution["validation_function"] != "0x400dff64" or execution["temperature_function"] != "0x400dff0c"):
        raise SystemExit("Unexpected original execution provenance")
    instruction_hashes = {}
    for address in ("400dff64", "400dff0c"):
        path = args.export / f"instructions/fn_{address}.json"
        instructions = json.loads(path.read_text())
        if any(x["mnemonic"].lower().startswith("loop") for x in instructions):
            raise SystemExit("Unmodeled Xtensa hardware loop")
        instruction_hashes["0x" + address] = digest(path)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = ["-std=c++11", "-O2", "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared"]
    counts, seen, goldens = collections.Counter(), set(), []
    selected_args = {0, 1, 2, 6, 7, 8, 9, 16, 17, 18, 63, 64, 65, 80, 96,
                     102, 103, 112, 113, 255, 256, 25087, 25088, 25089,
                     0x80000000, 0xf0000007, 0xf0000008, 0xffffffff}
    temperature_cases = 0
    with tempfile.TemporaryDirectory(prefix="zseries-instructions-") as directory:
        build = Path(directory)
        bridge = build / "bridge.cpp"
        bridge.write_text('''#include "zseries/runtime_instruction.hpp"
extern "C" int validity(unsigned op, unsigned arg) {
    const zseries::oem::Instruction input = {static_cast<zseries::oem::OpCode>(op), arg};
    return zseries::oem::instruction_valid(input);
}
extern "C" int temperature(unsigned op, unsigned arg, double* out) {
    const zseries::oem::Instruction input = {static_cast<zseries::oem::OpCode>(op), arg};
    return zseries::oem::instruction_temperature(input, *out);
}
''')
        library = build / "instructions.so"
        subprocess.run(compiler + flags + ["-I", str(ROOT / "include"), str(bridge),
                       str(ROOT / "src/runtime_instruction.cpp"), "-o", str(library)], check=True)
        native = ctypes.CDLL(str(library))
        native.validity.argtypes = [ctypes.c_uint, ctypes.c_uint]
        native.validity.restype = ctypes.c_int
        native.temperature.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_double)]
        native.temperature.restype = ctypes.c_int
        for row in csv.DictReader(args.original_csv.open()):
            opcode, argument = int(row["opcode"]), int(row["argument"])
            if (opcode, argument) in seen:
                raise SystemExit("Duplicate original case")
            seen.add((opcode, argument))
            expected = int(row["valid"])
            if native.validity(opcode, argument) != expected:
                raise SystemExit(f"Validator mismatch for {(opcode, argument)}")
            counts[expected] += 1
            temperature = ctypes.c_double(-42.25)
            decoded = native.temperature(opcode, argument, ctypes.byref(temperature))
            if row["celsius"]:
                temperature_cases += 1
                if not decoded or temperature.value != float(row["celsius"]):
                    raise SystemExit(f"Temperature decoder mismatch for {(opcode, argument)}")
            elif decoded or temperature.value != -42.25:
                raise SystemExit("Unsupported temperature opcode changed output")
            if argument in selected_args:
                goldens.append(row)
    if seen != set(cases) or temperature_cases != execution["temperature_cases"]:
        raise SystemExit("Missing original execution cases")
    report = {
        "schema_version": 1, "input_sha256": INPUT_SHA256,
        "validation_function": "0x400dff64", "temperature_function": "0x400dff0c",
        "scope": execution["scope"], "validation_cases": len(seen), "temperature_cases": temperature_cases,
        "all_results_match": True, "original_invalid_results": counts[0], "original_valid_results": counts[1],
        "portable_golden_cases": len(goldens), "instruction_export_sha256": instruction_hashes,
        "original_execution_csv_sha256": digest(args.original_csv),
        "original_execution_report_sha256": digest(str(args.original_csv) + ".json"),
        "native_compiler": subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0],
        "native_flags": flags,
        "source_sha256": {path: digest(ROOT / path) for path in (
            "include/zseries/runtime_instruction.hpp", "src/runtime_instruction.cpp",
            "tools/ghidra/EmulateRuntimeInstruction.java", "tools/validate_runtime_instruction.py")},
        "full_cpu_or_hardware_emulation": False, "hardware_tested": False,
    }
    if args.write_evidence:
        fixture = ROOT / "tests/fixtures/runtime-instruction-original.csv"
        with fixture.open("w") as stream:
            writer = csv.DictWriter(stream, ["opcode", "argument", "valid", "celsius"], lineterminator="\n")
            writer.writeheader()
            writer.writerows(goldens)
        report["portable_golden_sha256"] = digest(fixture)
        (ROOT / "analysis/runtime-instruction-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("validation_cases", "temperature_cases", "all_results_match", "portable_golden_cases")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
