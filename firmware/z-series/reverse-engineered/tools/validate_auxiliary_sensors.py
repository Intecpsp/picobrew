#!/usr/bin/env python3
"""Compare original auxiliary-sensor transactions, integer state and readings."""
from __future__ import annotations
import argparse
import copy
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"
KINDS = ["setup_lis", "tick_lis", "setup_bmp", "calibration_bmp", "tick_bmp"]
VALUES = ["acceleration_x", "acceleration_y", "acceleration_z", "pressure_pa", "estimated_boil_celsius"]
WORDS = ["address", "enabled", "raw_x", "raw_y", "raw_z", "raw_pressure", "raw_temperature", "temperature_fine",
         "pressure_ms", "now", "t1", "t2", "t3", "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8", "p9"]
COEFFICIENTS = [27504, 26435, -1000, 36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000]
FLAGS = ["-std=c++11", "-O2", "-fPIC", "-shared", "-fno-fast-math", "-ffp-contract=off"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def coefficients_bytes(values):
    return [b for value in values for b in (value & 255, (value >> 8) & 255)]


def case(name, kind, enabled=1):
    return {"id": name, "kind": kind, "values": [0.1, -0.2, 0.9, 100000., 99.],
            "words": [25 if kind.endswith("lis") else 118, enabled, 100, -200, 300,
                      415148, 519888, 1234, 100, 1000] + COEFFICIENTS[:], "ends": [],
            "replies": {"143": {"count": 1, "bytes": [51]}, "168": {"count": 6, "bytes": [0, 64, 0, 192, 255, 127]},
                        "208": {"count": 1, "bytes": [88]}, "136": {"count": 24, "bytes": coefficients_bytes(COEFFICIENTS)},
                        "247": {"count": 3, "bytes": [101, 90, 192]}, "250": {"count": 3, "bytes": [126, 237, 0]}}}


def input_cases():
    cases = []
    for kind in KINDS:
        for enabled in (0, 1):
            cases.append(case(f"normal-{kind}-{enabled}", kind, enabled))
    for kind, reg, offset in (("setup_lis", 143, 7), ("setup_bmp", 208, 1)):
        for enabled in (0, 1):
            for identity in (0, 1, 51, 88, 255, -1):
                item = case(f"identity-{kind}-{enabled}-{identity}", kind, enabled)
                item["replies"][str(reg)]["bytes"] = [identity]
                cases.append(item)
            for status in (1, 2, 4):
                item = case(f"id-end-{kind}-{enabled}-{status}", kind, enabled)
                item["ends"] = [0] * offset + [status]
                cases.append(item)
            item = case(f"id-short-{kind}-{enabled}", kind, enabled)
            item["replies"][str(reg)]["count"] = 0
            cases.append(item)
    for kind, reg, count in (("tick_lis", 168, 6), ("calibration_bmp", 136, 24), ("tick_bmp", 247, 3), ("tick_bmp", 250, 3)):
        for actual in (0, count - 1, count + 1):
            item = case(f"short-{kind}-{reg}-{actual}", kind)
            item["replies"][str(reg)]["count"] = actual
            cases.append(item)
        for status in (1, 2, 4):
            item = case(f"end-{kind}-{reg}-{status}", kind)
            item["ends"] = ([0] if reg == 250 else []) + [status]
            cases.append(item)
    for p1 in (0, 1, 32768, 65535):
        for raw in (0, 1, 0x7ffff, 0xfffff):
            item = case(f"pressure-edge-{p1}-{raw}", "tick_bmp")
            item["words"][13] = p1
            item["replies"]["247"]["bytes"] = [raw >> 12, (raw >> 4) & 255, (raw & 15) << 4]
            item["replies"]["250"]["bytes"] = item["replies"]["247"]["bytes"][:]
            cases.append(item)
    for low_byte in (0, 15, 16, 127, 128, 240, 255, -1):
        item = case(f"ignored-third-byte-{low_byte}", "tick_bmp")
        item["replies"]["247"]["bytes"][2] = low_byte
        item["replies"]["250"]["bytes"][2] = low_byte
        cases.append(item)
    for v in (-32768, -15987, -1, 0, 1, 15987, 32767):
        item = case(f"acceleration-extreme-{v}", "tick_lis")
        item["replies"]["168"]["bytes"] = coefficients_bytes([v] * 3)
        cases.append(item)
    generator = random.Random(119280)
    for index in range(512):
        kind = "tick_bmp" if index % 2 else "tick_lis"
        item = case(f"random-{index}", kind)
        item["values"][:3] = [generator.uniform(-2.1, 2.1) for _ in range(3)]
        item["words"][2:5] = [generator.randrange(-32768, 32768) for _ in range(3)]
        item["words"][5:7] = [generator.randrange(0x100000) for _ in range(2)]
        item["words"][9] = generator.randrange(0x100000000)
        if kind == "tick_bmp":
            item["words"][10:] = [generator.randrange(18000, 36000), generator.randrange(18000, 31000), generator.randrange(-4000, 1000),
                                  generator.randrange(26000, 44000), generator.randrange(-15000, -5000), generator.randrange(1000, 5000),
                                  generator.randrange(1000, 5000), generator.randrange(-1000, 1000), generator.randrange(-100, 100),
                                  generator.randrange(10000, 20000), generator.randrange(-20000, -8000), generator.randrange(3000, 9000)]
        for reply in item["replies"].values():
            reply["bytes"] = [generator.randrange(256) for _ in reply["bytes"]]
        if index % 8 == 0:
            item["ends"] = [generator.randrange(1, 5), generator.randrange(1, 5)]
        cases.append(item)
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    library = Path(directory) / "auxiliary-sensors.so"
    subprocess.run(compiler + FLAGS + ["-I", str(ROOT / "include"), str(ROOT / "tests/auxiliary_sensors_bridge.cpp"),
                   str(ROOT / "src/auxiliary_sensors.cpp"), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.auxiliary_action.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int64),
                                       ctypes.POINTER(ctypes.c_int), ctypes.c_size_t, ctypes.POINTER(ctypes.c_int), ctypes.c_size_t,
                                       ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_size_t), ctypes.c_int]
    native.auxiliary_action.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0]


def native_case(native, item, missing=False):
    values = (ctypes.c_double * 5)(*item["values"])
    words = (ctypes.c_int64 * 22)(*item["words"])
    ends = (ctypes.c_int * len(item["ends"]))(*item["ends"])
    data = []
    for reg, reply in item["replies"].items():
        data.extend([int(reg), reply["count"]] + reply["bytes"] + [0] * (24 - len(reply["bytes"])))
    replies = (ctypes.c_int * len(data))(*data)
    events = (ctypes.c_int * 1024)()
    count = ctypes.c_size_t()
    result = native.auxiliary_action(KINDS.index(item["kind"]), values, words, ends, len(ends), replies, len(item["replies"]),
                                     events, ctypes.byref(count), missing)
    if count.value > len(events) or count.value % 4:
        raise AssertionError("Unexpected auxiliary event shape")
    return result, {"id": item["id"], "words": list(words), "values": list(values),
                    "events": [list(events[i:i+4]) for i in range(0, count.value, 4)]}


def compare_case(native, item, expected):
    result, actual = native_case(native, item)
    if expected["id"] != item["id"] or actual["words"] != expected["words"] or actual["events"] != expected["events"]:
        differences = [(key, got, want) for key, got, want in zip(WORDS, actual["words"], expected["words"]) if got != want]
        raise AssertionError(f"Auxiliary state mismatch {item['id']}: {differences}; events match={actual['events']==expected['events']}")
    exact, maximum = 0, 0.
    for name, got, want in zip(VALUES, actual["values"], expected["values"]):
        exact += struct.pack("<d", got) == struct.pack("<d", want)
        maximum = max(maximum, abs(got - want))
        if not math.isclose(got, want, rel_tol=0, abs_tol=2e-12):
            raise AssertionError(f"Auxiliary value mismatch {item['id']} {name}: {got} != {want}")
    return exact, 5, maximum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-input", type=Path)
    parser.add_argument("--original-jsonl", type=Path)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--export", type=Path)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    cases = json.loads(args.input.read_text()) if args.input else input_cases()
    if args.write_evidence and args.input:
        parser.error("Evidence requires full deterministic input")
    if args.prepare_input:
        args.prepare_input.write_text(json.dumps(cases, separators=(",", ":")) + "\n")
        print(f"Prepared {len(cases)} auxiliary sensor scenarios")
    if not args.original_jsonl:
        if not args.prepare_input:
            parser.error("use --prepare-input or --original-jsonl")
        return 0
    if digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != IMAGE_SHA:
        raise SystemExit("Original identity mismatch")
    report_file = Path(str(args.original_jsonl) + ".json")
    execution = json.loads(report_file.read_text())
    original = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if execution["input_sha256"] != IMAGE_SHA or execution["cases"] != len(cases) or len(original) != len(cases):
        raise SystemExit("Unexpected original execution identity/count")
    exact, total, maximum, goldens = 0, 0, 0., []
    with tempfile.TemporaryDirectory(prefix="zseries-auxiliary-") as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, original):
            bits, count, error = compare_case(native, item, expected)
            exact += bits; total += count; maximum = max(maximum, error)
            if not item["id"].startswith("random-") or int(item["id"].split("-")[-1]) < 24:
                goldens.append({"input": item, "original": expected})
    report = dict(execution, all_results_match=True, binary64_comparisons=total, exact_binary64_matches=exact,
                  maximum_absolute_error=maximum, portable_golden_cases=len(goldens), native_compiler=compiler, native_flags=FLAGS,
                  original_execution_jsonl_sha256=digest(args.original_jsonl), original_execution_report_sha256=digest(report_file))
    if args.write_evidence:
        if not args.export:
            parser.error("Evidence needs --export")
        report["instruction_export_sha256"] = {address: digest(args.export / "instructions" / ("fn_" + address[2:] + ".json"))
                                               for address in execution["original_functions_executed"]}
        report["source_sha256"] = {path: digest(ROOT / path) for path in ("include/zseries/auxiliary_sensors.hpp", "src/auxiliary_sensors.cpp",
                                  "tests/auxiliary_sensors_bridge.cpp", "tools/validate_auxiliary_sensors.py", "tools/ghidra/EmulateAuxiliarySensors.java",
                                  "tools/ghidra/Z119Execution.java", "analysis/auxiliary-math-symbols.json")}
        fixture = ROOT / "tests/fixtures/auxiliary-sensors-original.json"
        fixture.write_text(json.dumps({"schema_version": 1, "input_sha256": IMAGE_SHA, "words": WORDS, "values": VALUES, "cases": goldens}, separators=(",", ":")) + "\n")
        report["portable_golden_sha256"] = digest(fixture)
        (ROOT / "analysis/auxiliary-sensors-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("cases", "all_results_match", "binary64_comparisons", "exact_binary64_matches", "portable_golden_cases")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
