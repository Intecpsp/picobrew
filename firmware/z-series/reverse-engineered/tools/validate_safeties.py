#!/usr/bin/env python3
"""Compare timed safety interlocks and GPIO reads with original Z119 bodies."""
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
VALUES = ["hex", "wort", "drain", "hex_rate", "hex_rise_threshold", "hex_wort_threshold", "wort_drain_threshold"]
WORDS = ["enabled", "hot", "cold", "old", "hex_wort_tripped", "wort_drain_tripped", "hex_rise_tripped", "filter_missing",
         "previous_hot", "previous_cold", "previous_old", "hex_rise_enabled", "hex_rise_deadline", "hex_wort_enabled",
         "hex_wort_deadline", "wort_drain_enabled", "wort_drain_deadline", "pumps", "valve", "filter_pin", "now"]
FLAGS = ["-std=c++11", "-O2", "-fPIC", "-shared", "-fno-fast-math", "-ffp-contract=off"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def case(name, ticks=None, values=None, words=None):
    numbers = [20., 20., 20., 0., 0., 0., 0.]
    integers = [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 1000]
    for key, value in (values or {}).items():
        numbers[VALUES.index(key)] = float(value)
    for key, value in (words or {}).items():
        integers[WORDS.index(key)] = value & 0xffffffff
    if ticks is None:
        ticks = [[integers[20], integers[19], integers[17], integers[18], *numbers[:4]]]
    return {"id": name, "values": numbers, "words": integers, "ticks": ticks}


def input_cases():
    cases = []
    for enabled in (0, 1):
        for pumps in (0, 8, 16, 32, 64, 128, 240, 255):
            for valve in list(range(12)) + [255]:
                cases.append(case(f"pump-valve-{enabled}-{pumps}-{valve}", values={"hex": 60, "drain": 80},
                                  words={"enabled": enabled, "pumps": pumps, "valve": valve, "filter_pin": valve % 3,
                                         "hot": 1, "cold": 2, "old": 3}))
    for monitor, threshold in (("hex_rise", 40.), ("hex_wort", 15.), ("wort_drain", 50.)):
        for tripped in (0, 1):
            for offset in (-1, 0, 1):
                for delta in (-0.0001, 0., 0.0001):
                    words = {monitor + "_enabled": 1, monitor + "_deadline": 5000,
                             monitor + "_tripped": tripped, "now": 5000 + offset, "pumps": 0 if monitor == "hex_rise" else 16}
                    values = {monitor + "_threshold": threshold}
                    values["drain" if monitor == "wort_drain" else "hex"] = threshold + delta + (0 if monitor == "hex_rise" else 20)
                    cases.append(case(f"threshold-{monitor}-{tripped}-{offset}-{delta}", values=values, words=words))
    for rate in (-0.001, 0., 0.001):
        for tripped in (0, 1):
            cases.append(case(f"cooling-{rate}-{tripped}", values={"hex": 50, "hex_rate": rate, "hex_wort_threshold": 15},
                              words={"pumps": 16, "hex_wort_enabled": 1, "hex_wort_deadline": 1, "hex_wort_tripped": tripped}))
    for now in (0, 1, 0xffffff00, 0xffffffff):
        cases.append(case(f"deadline-rollover-{now}", values={"hex": 90, "drain": 100}, words={"now": now, "pumps": 16}))
    cases.append(case("pump-rise-and-delta-sequence", ticks=[
        [1000, 0, 0, 4, 20, 20, 20, 0], [2000, 0, 0, 4, 40, 20, 20, 0],
        [3000, 0, 0, 4, 40.001, 20, 20, 0], [4000, 1, 16, 4, 40, 20, 80, 0],
        [8999, 1, 16, 4, 40, 20, 80, 0], [9000, 1, 16, 4, 40, 20, 80, 0],
        [304000, 1, 16, 3, 40, 20, 80, 0], [305000, 0, 0, 3, 40, 20, 80, 0]]))
    for monitor in ("hex_rise", "hex_wort", "wort_drain"):
        cases.append(case(f"disabled-latched-{monitor}", words={monitor + "_tripped": 1, "pumps": 16 if monitor == "hex_rise" else 0}))
    generator = random.Random(119)
    for index in range(384):
        values = {key: generator.uniform(-20, 120) for key in VALUES}
        words = {"enabled": index % 2, "hot": generator.randrange(16), "cold": generator.randrange(16), "old": generator.randrange(16),
                 "previous_hot": generator.randrange(16), "previous_cold": generator.randrange(16), "previous_old": generator.randrange(16)}
        for monitor in ("hex_rise", "hex_wort", "wort_drain"):
            words[monitor + "_enabled"] = generator.randrange(2)
            words[monitor + "_tripped"] = generator.randrange(2)
            words[monitor + "_deadline"] = generator.choice([0, 5000, 300000, 0xfffffff0])
        ticks = [[generator.choice([1, 4999, 5000, 5001, 300000, 0xfffffff0]), generator.randrange(3),
                  generator.randrange(256), generator.randrange(12)] + [generator.uniform(-20, 125) for _ in range(4)] for _ in range(3)]
        cases.append(case(f"random-{index}", ticks=ticks, values=values, words=words))
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    library = Path(directory) / "safeties.so"
    subprocess.run(compiler + FLAGS + ["-I", str(ROOT / "include"), str(ROOT / "tests/safeties_bridge.cpp"),
                   str(ROOT / "src/safeties.cpp"), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.safeties_action.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint32),
                                      ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    native.safeties_action.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0]


def compare_case(native, item, expected):
    if item["id"] != expected["id"] or len(item["ticks"]) != len(expected["snapshots"]):
        raise AssertionError("Original safety identity/count mismatch")
    values = (ctypes.c_double * 7)(*item["values"])
    words = (ctypes.c_uint32 * 21)(*item["words"])
    reads, exact, total, maximum_error = 0, 0, 0, 0.
    for index, (tick, snapshot) in enumerate(zip(item["ticks"], expected["snapshots"])):
        words[20], words[19], words[17], words[18] = tick[:4]
        for position in range(4):
            values[position] = tick[4 + position]
        count = ctypes.c_uint32()
        result = native.safeties_action(values, words, ctypes.byref(count), 0)
        reads += count.value
        if result != 1 or list(words) != snapshot["words"] or reads != snapshot["pin_reads"]:
            difference = [(name, got, want) for name, got, want in zip(WORDS, words, snapshot["words"]) if got != want]
            raise AssertionError(f"Safety state mismatch {item['id']} tick {index}: {difference}; reads {reads}/{snapshot['pin_reads']}")
        for name, actual, wanted in zip(VALUES, values, snapshot["values"]):
            total += 1
            exact += struct.pack("<d", actual) == struct.pack("<d", wanted)
            maximum_error = max(maximum_error, abs(actual - wanted))
            if not math.isclose(actual, wanted, rel_tol=0, abs_tol=2e-12):
                raise AssertionError(f"Safety number mismatch {item['id']} {name}: {actual} != {wanted}")
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
        parser.error("Evidence requires full deterministic cases")
    if args.prepare_input:
        args.prepare_input.write_text(json.dumps(cases, separators=(",", ":")) + "\n")
        print(f"Prepared {len(cases)} safety scenarios and {sum(len(x['ticks']) for x in cases)} ticks")
    if not args.original_jsonl:
        if not args.prepare_input:
            parser.error("use --prepare-input or --original-jsonl")
        return 0
    if digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != IMAGE_SHA:
        raise SystemExit("Original image identity mismatch")
    execution = json.loads(Path(str(args.original_jsonl) + ".json").read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if execution["input_sha256"] != IMAGE_SHA or execution["cases"] != len(cases) or len(originals) != len(cases):
        raise SystemExit("Unexpected original execution identity/count")
    exact, total, maximum_error, goldens = 0, 0, 0., []
    with tempfile.TemporaryDirectory(prefix="zseries-safeties-") as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, originals):
            bit_matches, comparisons, error = compare_case(native, item, expected)
            exact += bit_matches; total += comparisons; maximum_error = max(maximum_error, error)
            if not item["id"].startswith("random-") or int(item["id"].split("-")[-1]) < 16:
                goldens.append({"input": item, "original": expected})
    report = {"schema_version": 1, "input_sha256": IMAGE_SHA, "scope": execution["scope"], "cases": len(cases),
              "ticks": execution["ticks"], "all_results_match": True, "binary64_comparisons": total,
              "exact_binary64_matches": exact, "maximum_absolute_error": maximum_error,
              "original_functions_executed": execution["original_functions_executed"], "modeled_boundaries": execution["modeled_boundaries"],
              "original_execution_jsonl_sha256": digest(args.original_jsonl),
              "original_execution_report_sha256": digest(str(args.original_jsonl) + ".json"),
              "native_compiler": compiler, "native_flags": FLAGS, "portable_golden_cases": len(goldens),
              "full_cpu_or_hardware_emulation": False, "hardware_tested": False}
    if args.write_evidence:
        if not args.export:
            parser.error("--write-evidence requires --export")
        report["instruction_export_sha256"] = {address: digest(args.export / "instructions" / ("fn_" + address[2:] + ".json"))
                                               for address in execution["original_functions_executed"]}
        report["source_sha256"] = {path: digest(ROOT / path) for path in ("include/zseries/safeties.hpp", "src/safeties.cpp",
                                  "tests/safeties_bridge.cpp", "tools/validate_safeties.py", "tools/ghidra/EmulateSafeties.java",
                                  "tools/ghidra/Z119Execution.java")}
        fixture = ROOT / "tests/fixtures/safeties-original.json"
        fixture.write_text(json.dumps({"schema_version": 1, "input_sha256": IMAGE_SHA, "values": VALUES, "words": WORDS,
                                      "cases": goldens}, separators=(",", ":")) + "\n")
        report["portable_golden_sha256"] = digest(fixture)
        (ROOT / "analysis/safeties-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("cases", "ticks", "all_results_match", "exact_binary64_matches", "portable_golden_cases")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
