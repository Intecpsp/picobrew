#!/usr/bin/env python3
"""Compare actual ADC transaction ordering and smoother states to Z119 code."""
from __future__ import annotations
import argparse
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
FLAGS = ["-std=c++11", "-O2", "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def input_cases():
    cases = []
    for boiler in (0, 1):
        for address in (0x34, 0x35):
            cases.append({"id": f"setup-{boiler}-{address}", "kind": "setup", "address": address, "enabled": 0,
                          "types": [4] * 4, "raw": [15, 30, 4095, -1], "boiler": boiler})
    for count in list(range(8)) + [9, 255]:
        cases.append({"id": f"short-reply-{count}", "kind": "poll", "address": 0x34, "enabled": 1,
                      "types": [0, 1, 1, 2], "raw": [123] * 4, "request_count": count, "bytes": []})
    generator = random.Random(119)
    byte_sets = [[0, 0, 0, 14, 0, 15, 15, 254], [15, 255, 255, 255, 16, 0, 255, 254]]
    byte_sets += [[generator.randrange(256) for _ in range(8)] for _ in range(128)]
    for index, values in enumerate(byte_sets):
        cases.append({"id": f"poll-{index}", "kind": "poll", "address": 0x34, "enabled": index % 2,
                      "types": [0, 1, 1, 2], "raw": [-1] * 4, "request_count": 8, "bytes": values})
    sequences = [
        ("fill-wrap", [[i * 10 + 1, i * 3.125] for i in range(32)]),
        ("gap-boundaries", [[1, 20.], [2001, 40.], [4002, 60.], [4003, 80.], [6003, 100.]]),
        ("zero-time", [[0, 20.], [5000, 40.], [5001, 60.]]),
        ("rollover", [[0xfffffff0, 12.], [0x10, 14.], [0x7e0, 16.], [0x1000, 18.]]),
    ]
    for name, updates in sequences:
        cases.append({"id": name, "kind": "smooth", "values": [0.] * 12, "words": [0] * 4, "updates": updates})
    for index in range(192):
        samples = [generator.uniform(-20, 125) for _ in range(10)]
        count = index % 11
        next_index = count if count < 10 else generator.randrange(10)
        last = generator.randrange(1, 100000)
        initial_sum = sum(samples[:count]) if count else 0.
        values = samples + [initial_sum, initial_sum / count if count else 0.]
        updates = []
        now = last
        for _ in range(16):
            now = (now + generator.choice([0, 1, 10, 2000, 2001])) & 0xffffffff
            updates.append([now, generator.uniform(-20, 125)])
        cases.append({"id": f"smooth-random-{index}", "kind": "smooth", "values": values,
                      "words": [next_index, count, last, last], "updates": updates})
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    library = Path(directory) / "acquisition.so"
    subprocess.run(compiler + FLAGS + ["-I", str(ROOT / "include"), str(ROOT / "tests/acquisition_bridge.cpp"),
                   str(ROOT / "src/sensor_acquisition.cpp"), str(ROOT / "src/temperature.cpp"), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.adc_action.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32, ctypes.c_uint32,
                                 ctypes.POINTER(ctypes.c_int), ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32),
                                 ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    native.adc_action.restype = ctypes.c_int
    native.smoother_action.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint32), ctypes.c_double, ctypes.c_uint32]
    native.smoother_action.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0]


def run_adc(native, item, missing_io=False):
    words = (ctypes.c_uint32 * 10)(item["address"], item["enabled"], *item["types"], *item["raw"])
    supplied = item.get("bytes", [])
    bytes_ = (ctypes.c_int * len(supplied))(*supplied)
    events, count = (ctypes.c_uint32 * 64)(), ctypes.c_uint32()
    result = native.adc_action(item["kind"] == "setup", words, item.get("boiler", 0), item.get("request_count", 0),
                               bytes_, len(supplied), events, ctypes.byref(count), missing_io)
    if count.value > 16:
        raise AssertionError("Event bound exceeded")
    return result, {"types": list(words[2:6]), "raw": [ctypes.c_int32(x).value for x in words[6:]],
                    "enabled": words[1], "events": [list(events[i * 4:i * 4 + 4]) for i in range(count.value)]}


def compare_case(native, item, expected):
    if item["id"] != expected["id"]:
        raise AssertionError("Original identity mismatch")
    if item["kind"] != "smooth":
        result, output = run_adc(native, item)
        wanted_result = 0 if item["kind"] == "setup" or item["request_count"] == 8 else 3
        if result != wanted_result:
            raise AssertionError("Native result mismatch")
        expected = {key: expected[key] for key in ("types", "raw", "enabled", "events")}
        expected["events"] = [entry + [0] * (4 - len(entry)) for entry in expected["events"]]
        if output != expected:
            raise AssertionError(f"ADC mismatch {item['id']}: {output!r} != {expected!r}")
        return 0, 0, 0.
    values = (ctypes.c_double * 12)(*item["values"])
    words = (ctypes.c_uint32 * 4)(*item["words"])
    if len(item["updates"]) != len(expected["snapshots"]):
        raise AssertionError("Original smoother length mismatch")
    exact, total, maximum_error = 0, 0, 0.
    for index, ((now, value), snapshot) in enumerate(zip(item["updates"], expected["snapshots"])):
        result = native.smoother_action(values, words, value, now)
        if result != 0 or list(words) != snapshot["words"]:
            raise AssertionError(f"Smoother state mismatch {item['id']} update {index}")
        for actual, wanted in zip(values, snapshot["values"]):
            total += 1
            exact += struct.pack("<d", actual) == struct.pack("<d", wanted)
            maximum_error = max(maximum_error, abs(actual - wanted))
            if not math.isclose(actual, wanted, rel_tol=0, abs_tol=2e-12):
                raise AssertionError(f"Smoother number mismatch {item['id']} update {index}: {actual} != {wanted}")
    return exact, total, maximum_error


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
        parser.error("Evidence requires the full deterministic case set")
    if args.prepare_input:
        args.prepare_input.write_text(json.dumps(cases, separators=(",", ":")) + "\n")
        print(f"Prepared {len(cases)} cases and {sum(len(x.get('updates', [])) for x in cases)} smoother updates")
    if not args.original_jsonl:
        if not args.prepare_input:
            parser.error("use --prepare-input or --original-jsonl")
        return 0
    if digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != IMAGE_SHA:
        raise SystemExit("Original image identity mismatch")
    execution = json.loads(Path(str(args.original_jsonl) + ".json").read_text())
    if execution["input_sha256"] != IMAGE_SHA or execution["cases"] != len(cases):
        raise SystemExit("Unexpected original execution provenance")
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if len(originals) != len(cases):
        raise SystemExit("Original case count mismatch")
    exact, total, maximum_error, goldens = 0, 0, 0., []
    with tempfile.TemporaryDirectory(prefix="zseries-acquisition-") as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, originals):
            bit_matches, comparisons, error = compare_case(native, item, expected)
            exact += bit_matches; total += comparisons; maximum_error = max(maximum_error, error)
            if not item["id"].startswith("smooth-random-") or int(item["id"].split("-")[-1]) < 16:
                goldens.append({"input": item, "original": expected})
    report = {"schema_version": 1, "input_sha256": IMAGE_SHA, "scope": execution["scope"],
              "cases": len(cases), "smoother_updates": execution["smoother_updates"], "all_results_match": True,
              "binary64_comparisons": total, "exact_binary64_matches": exact, "maximum_absolute_error": maximum_error,
              "original_functions_executed": execution["original_functions_executed"], "modeled_boundaries": execution["modeled_boundaries"],
              "original_execution_jsonl_sha256": digest(args.original_jsonl),
              "original_execution_report_sha256": digest(str(args.original_jsonl) + ".json"),
              "native_compiler": compiler, "native_flags": FLAGS, "portable_golden_cases": len(goldens),
              "hardware_tested": False, "full_cpu_or_hardware_emulation": False}
    if args.write_evidence:
        if not args.export:
            parser.error("--write-evidence requires --export")
        report["instruction_export_sha256"] = {address: digest(args.export / "instructions" / ("fn_" + address[2:] + ".json"))
                                                for address in execution["original_functions_executed"]}
        report["source_sha256"] = {path: digest(ROOT / path) for path in ("src/sensor_acquisition.cpp", "include/zseries/sensor_acquisition.hpp",
                                  "tests/acquisition_bridge.cpp", "tools/validate_acquisition.py", "tools/ghidra/EmulateAcquisition.java")}
        fixture = ROOT / "tests/fixtures/acquisition-original.json"
        fixture.write_text(json.dumps({"schema_version": 1, "input_sha256": IMAGE_SHA, "cases": goldens}, separators=(",", ":")) + "\n")
        report["portable_golden_sha256"] = digest(fixture)
        (ROOT / "analysis/acquisition-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("cases", "smoother_updates", "all_results_match", "binary64_comparisons",
                                                 "exact_binary64_matches", "maximum_absolute_error", "portable_golden_cases")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
