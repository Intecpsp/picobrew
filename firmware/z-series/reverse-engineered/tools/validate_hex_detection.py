#!/usr/bin/env python3
"""Prepare sensor-detection cases or compare owned source to original execution."""
from __future__ import annotations
import argparse
import collections
import csv
import ctypes
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INPUT_SHA256 = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"
FIELDS = ["ambient", "wort", "drain", "senstech_hex", "aet_hex"]
EDGES = [-10., 0., 14.999, 15., 15.001, 19., 20., 24.999, 25., 25.001,
         25.999, 26., 34.999, 35., 35.001, 36., 70.]


def input_cases():
    references = itertools.product([20., 25., 25.999, 26., 35., 35.001], repeat=3)
    candidates = list(itertools.product([15., 20., 24.999, 25., 26., 40.], repeat=2))
    rows = {reference + candidate for reference in references for candidate in candidates}
    for base in ([20., 20., 20., 20., 70.], [20., 20., 20., 70., 20.]):
        for index in range(5):
            for value in EDGES:
                row = base.copy()
                row[index] = value
                rows.add(tuple(row))
    generator = random.Random(119)
    for _ in range(512):
        rows.add(tuple(20 + generator.randrange(-16000, 16001) / 1000 for _ in range(5)))
    return sorted(rows)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-input", type=Path)
    parser.add_argument("--original-csv", type=Path)
    parser.add_argument("--export", type=Path)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    rows = input_cases()
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True, exist_ok=True)
        with args.prepare_input.open("w") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(FIELDS)
            writer.writerows(rows)
        print(f"Prepared {len(rows)} sensor-detection cases")
    if not args.original_csv:
        if not args.prepare_input:
            parser.error("use --prepare-input or --original-csv")
        return 0
    if not args.export:
        parser.error("--original-csv requires --export")
    if digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != INPUT_SHA256:
        raise SystemExit("Preserved input identity mismatch")
    execution = json.loads(Path(str(args.original_csv) + ".json").read_text())
    if (execution["input_sha256"] != INPUT_SHA256 or execution["function"] != "0x400dd9f4" or
            execution["cases"] != len(rows)):
        raise SystemExit("Unexpected original execution provenance")
    instruction_file = args.export / "instructions/fn_400dd9f4.json"
    instructions = json.loads(instruction_file.read_text())
    if any(x["mnemonic"].lower().startswith("loop") for x in instructions):
        raise SystemExit("Unmodeled Xtensa hardware loop")
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = ["-std=c++11", "-O2", "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared"]
    counts, fixture_counts = collections.Counter(), collections.Counter()
    goldens, seen = [], set()
    boundary_inputs = set()
    for base in ([20., 20., 20., 20., 70.], [20., 20., 20., 70., 20.]):
        for index in range(5):
            for value in (14.999, 15., 24.999, 25., 25.999, 26., 35., 35.001):
                vector = base.copy()
                vector[index] = value
                boundary_inputs.add(tuple(vector))
    with tempfile.TemporaryDirectory(prefix="zseries-hex-") as directory:
        build = Path(directory)
        source = build / "bridge.cpp"
        source.write_text('''#include "zseries/hex_detection.hpp"
extern "C" int reconstructed(const double* p, unsigned* sensor) {
    using namespace zseries::oem;
    const HexTemperatures input = {p[0], p[1], p[2], p[3], p[4]};
    HexDetection result;
    if (!evaluate_hex_detection(input, result)) return -1;
    *sensor = static_cast<unsigned>(result.sensor);
    return static_cast<int>(result.error);
}
''')
        library = build / "hex.so"
        subprocess.run(compiler + flags + ["-I", str(ROOT / "include"), str(source),
                       str(ROOT / "src/temperature.cpp"), str(ROOT / "src/hex_detection.cpp"),
                       "-o", str(library)], check=True)
        native = ctypes.CDLL(str(library)).reconstructed
        native.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint)]
        native.restype = ctypes.c_int
        for row in csv.DictReader(args.original_csv.open()):
            vector = tuple(float(row[field]) for field in FIELDS)
            if vector in seen:
                raise SystemExit("Duplicate original case")
            seen.add(vector)
            expected_error, expected_sensor = int(row["error"]), int(row["sensor_type"])
            sensor = ctypes.c_uint()
            error = native((ctypes.c_double * 5)(*vector), ctypes.byref(sensor))
            if (error, sensor.value) != (expected_error, expected_sensor):
                raise SystemExit(f"Mismatch {vector}: original {(expected_error, expected_sensor)}, owned {(error, sensor.value)}")
            counts[error] += 1
            key = (error, sensor.value)
            if fixture_counts[key] < 4 or vector in boundary_inputs:
                goldens.append({"temperatures": list(vector), "error": error, "sensor_type": sensor.value})
                fixture_counts[key] += 1
    if seen != set(rows) or set(counts) != set(range(9)):
        raise SystemExit("Original execution misses an expected input or error branch")
    report = {
        "schema_version": 1, "input_sha256": INPUT_SHA256, "function": "0x400dd9f4",
        "scope": execution["scope"], "cases": len(seen), "matching_cases": len(seen),
        "original_error_counts": dict(sorted(counts.items())), "instruction_count": len(instructions),
        "hardware_loop_instructions": 0, "golden_cases": len(goldens),
        "original_execution_csv_sha256": digest(args.original_csv),
        "original_execution_report_sha256": digest(str(args.original_csv) + ".json"),
        "instruction_export_sha256": digest(instruction_file),
        "native_compiler": subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0],
        "native_flags": flags,
        "source_sha256": {path: digest(ROOT / path) for path in (
            "include/zseries/hex_detection.hpp", "src/hex_detection.cpp",
            "tools/ghidra/EmulateHexDetection.java", "tools/validate_hex_detection.py")},
        "full_cpu_or_hardware_emulation": False, "hardware_tested": False,
    }
    if args.write_evidence:
        fixture = {"schema_version": 1, "input_sha256": INPUT_SHA256, "function": "0x400dd9f4",
                   "scope": execution["scope"], "full_execution_cases": len(seen), "cases": goldens}
        (ROOT / "tests/fixtures/hex-detection-original.json").write_text(json.dumps(fixture, indent=2) + "\n")
        (ROOT / "analysis/hex-detection-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("cases", "matching_cases", "original_error_counts", "golden_cases")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
