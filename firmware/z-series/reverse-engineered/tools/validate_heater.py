#!/usr/bin/env python3
"""Compare recovered heater state and pin events with original Xtensa bodies."""
from __future__ import annotations
import argparse
import collections
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
ACTIONS = ["tunings", "initialize_pid", "mode", "limits", "direction", "compute",
           "configure_boiler", "initialize_outputs", "enable", "start", "stop", "target", "tick20", "tick1"]
VALUE_NAMES = ["display_kp", "display_ki", "display_kd", "kp", "ki", "kd", "minimum", "maximum",
               "error", "proportional", "integral", "derivative", "previous_input", "target", "target_excess",
               "output_ticks", "integral_display", "previous_wort", "previous_hex", "wort_rate", "hex_rate",
               "estimated_boil", "hex", "wort", "drain", "ambient"]
WORD_NAMES = ["automatic", "sample_time_ms", "reverse", "enabled", "started", "countdown_bits", "second_phase",
              "safety_enabled", "too_hot", "too_cold", "too_old", "hex_updated", "wort_updated", "drain_updated",
              "ambient_updated", "now_ms", "output_pin"]
FLAGS = ["-std=c++11", "-O2", "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(kind, *args):
    return {"kind": kind, "args": list(args)}


def case(name, actions, values=None, words=None):
    initial_values = [34., 13., 3., 34., 1.3, 30., 0., 100., 3., 4., 12., 5.,
                      19., 60., 0., 20., 12., 19., 19., 1., -1., 100., 20., 20., 20., 20.]
    initial_words = [0, 100, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1000, 1000, 1000, 1000, 2000, 12]
    for key, value in (values or {}).items():
        initial_values[VALUE_NAMES.index(key)] = float(value)
    for key, value in (words or {}).items():
        initial_words[WORD_NAMES.index(key)] = value & 0xffffffff
    return {"id": name, "values": initial_values, "words": initial_words, "actions": actions}


def input_cases():
    cases = []
    for kind, args in [("tunings", [34, 13, 3]), ("initialize_pid", []), ("mode", [1]),
                       ("limits", [0, 100]), ("direction", [1]), ("compute", []),
                       ("configure_boiler", [0]), ("configure_boiler", [1]), ("initialize_outputs", []),
                       ("enable", [0]), ("enable", [1]), ("start", []), ("stop", []), ("target", [105]),
                       ("tick20", []), ("tick1", [])]:
        cases.append(case(f"operation-{kind}-{len(cases)}", [command(kind, *args)]))
    for automatic in (0, 1):
        for mode in (0, 1):
            for integral in (-12.5, 0., 104.25):
                cases.append(case(f"mode-{automatic}-{mode}-{integral}", [command("mode", mode)],
                                  {"integral": integral, "minimum": 7.5}, {"automatic": automatic}))
        for reverse in (0, 1):
            for target in (0, 1):
                cases.append(case(f"direction-{automatic}-{reverse}-{target}", [command("direction", target)],
                                  words={"automatic": automatic, "reverse": reverse}))
        for minimum, maximum in ((0., 100.), (-5., 12.5), (7., 7.), (9., 8.)):
            for output, integral in ((-30., -20.), (3., 5.), (150., 200.)):
                cases.append(case(f"limits-{automatic}-{minimum}-{maximum}-{output}",
                                  [command("limits", minimum, maximum)],
                                  {"output_ticks": output, "integral": integral}, {"automatic": automatic}))
    for sample in (1, 100, 1000, 2000):
        for reverse in (0, 1):
            for gains in ((0., 0., 0.), (34., 13., 3.), (0.125, 2.5, 0.75), (-1., 3., 4.),
                          (1., -3., 4.), (1., 3., -4.)):
                cases.append(case(f"gains-{sample}-{reverse}-{gains}", [command("tunings", *gains)],
                                  words={"sample_time_ms": sample, "reverse": reverse}))
    for boil in (90., 100., 105.):
        for offset in (-200., -0.001, 0., 0.001, 2., 50.):
            cases.append(case(f"target-{boil}-{offset}", [command("target", boil - 2 + offset)],
                              {"estimated_boil": boil, "target_excess": 3.}))
    for started in (0, 1):
        for countdown in (-2, -1, 0, 1, 2, 100, 0x7fffffff):
            cases.append(case(f"countdown-{started}-{countdown}", [command("tick20")] * 3,
                              words={"started": started, "countdown_bits": countdown}))
    for phase in (0, 1):
        for started in (0, 1):
            for safety in (0, 1):
                cases.append(case(f"phase-{phase}-{started}-{safety}", [command("tick1")] * 3,
                                  words={"second_phase": phase, "started": started, "automatic": 1,
                                         "safety_enabled": safety, "enabled": 0}))
    for channel, limits in zip(("hex", "wort", "drain", "ambient"), ((0., 110.), (0., 105.), (0., 105.), (-20., 62.))):
        for bound in limits:
            for delta in (-0.001, 0., 0.001):
                cases.append(case(f"safety-temperature-{channel}-{bound}-{delta}", [command("tick1")],
                                  {channel: bound + delta}, {"started": 1, "automatic": 1}))
        for now, updated in ((5000, 0), (5000, 1000), (5001, 1000), (0x10, 0xfffffff0), (0x10, 0x11)):
            cases.append(case(f"safety-age-{channel}-{now}-{updated}", [command("tick1")],
                              words={"started": 1, "automatic": 1, "now_ms": now, channel + "_updated": updated}))
    for mask in ("too_hot", "too_cold", "too_old"):
        for enabled in (0, 1):
            cases.append(case(f"retained-{mask}-{enabled}", [command("tick1")],
                              words={"safety_enabled": 0, mask: 8, "automatic": 1, "started": 1, "enabled": enabled,
                                     "countdown_bits": 77}))
    actions = [command("configure_boiler", 1), command("initialize_outputs"), command("enable", 1),
               command("target", 60), command("start"), command("tick1")]
    actions += [command("tick20")] * 102
    actions += [command("tick1"), command("tick1"), command("stop"), command("enable", 0)]
    cases.append(case("full-output-window", actions))
    generator = random.Random(119)
    for index in range(384):
        values = {"wort": generator.uniform(-5, 110), "hex": generator.uniform(-5, 115),
                  "drain": generator.uniform(-2, 108), "ambient": generator.uniform(-22, 65),
                  "previous_input": generator.uniform(0, 100), "previous_wort": generator.uniform(0, 100),
                  "previous_hex": generator.uniform(0, 110), "wort_rate": generator.uniform(-10, 10),
                  "hex_rate": generator.uniform(-10, 10), "target": generator.uniform(-5, 98),
                  "integral": generator.uniform(-10, 110), "output_ticks": generator.uniform(0, 100),
                  "kp": generator.uniform(0, 40), "ki": generator.uniform(0, 5), "kd": generator.uniform(0, 50)}
        words = {"automatic": index % 2, "started": 1, "enabled": (index // 2) % 2,
                 "safety_enabled": (index // 4) % 2, "countdown_bits": index % 103,
                 "hex_updated": generator.randrange(1, 7000), "wort_updated": generator.randrange(1, 7000),
                 "now_ms": 7000, "output_pin": 12 if index % 3 else 17}
        actions = [command("compute")] if index % 2 else [command("tick1"), command("tick20")]
        cases.append(case(f"random-{index}", actions, values, words))
    if len({item["id"] for item in cases}) != len(cases):
        raise RuntimeError("Duplicate generated case")
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    library = Path(directory) / "heater.so"
    subprocess.run(compiler + FLAGS + ["-I", str(ROOT / "include"), str(ROOT / "tests/heater_bridge.cpp"),
                   str(ROOT / "src/heater.cpp"), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.heater_action.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint32),
                                    ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint32),
                                    ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    native.heater_action.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0]


def invoke_native(native, action, values, words, missing_io=False):
    numbers = (ctypes.c_double * 26)(*values)
    integers = (ctypes.c_uint32 * 17)(*words)
    arguments = (ctypes.c_double * 3)(*(action["args"] + [0.] * (3 - len(action["args"]))))
    events = (ctypes.c_uint32 * 48)()
    count = ctypes.c_uint32()
    result = native.heater_action(ACTIONS.index(action["kind"]), numbers, integers, arguments,
                                 events, ctypes.byref(count), missing_io)
    if count.value > 16:
        raise AssertionError("Native event bound exceeded")
    return result, list(numbers), list(integers), [list(events[i * 3:i * 3 + 3]) for i in range(count.value)]


def compare_case(native, original_input, expected):
    if original_input["id"] != expected["id"] or len(original_input["actions"]) != len(expected["snapshots"]):
        raise AssertionError("Original case identity/length mismatch")
    values, words, events = original_input["values"], original_input["words"], []
    exact, comparisons, maximum_error = 0, 0, 0.
    for step, (action, snapshot) in enumerate(zip(original_input["actions"], expected["snapshots"])):
        result, values, words, new_events = invoke_native(native, action, values, words)
        if result not in (0, 1):
            raise AssertionError("Unknown native action")
        events.extend(new_events)
        if words != snapshot["words"] or events != snapshot["gpio"]:
            changed = [(name, actual, wanted) for name, actual, wanted in zip(WORD_NAMES, words, snapshot["words"]) if actual != wanted]
            raise AssertionError(f"{original_input['id']} step {step}: words {changed}; GPIO {events} != {snapshot['gpio']}")
        for name, actual, wanted in zip(VALUE_NAMES, values, snapshot["values"]):
            comparisons += 1
            if struct.pack("<d", actual) == struct.pack("<d", wanted):
                exact += 1
            difference = abs(actual - wanted)
            maximum_error = max(maximum_error, difference)
            if not math.isfinite(actual) or not math.isclose(actual, wanted, rel_tol=0, abs_tol=2e-12):
                raise AssertionError(f"{original_input['id']} step {step}, {name}: {actual!r} != {wanted!r}")
    return exact, comparisons, maximum_error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-input", type=Path)
    parser.add_argument("--original-jsonl", type=Path)
    parser.add_argument("--input", type=Path, help="Only for an initial smoke comparison; evidence uses the deterministic full set")
    parser.add_argument("--export", type=Path)
    parser.add_argument("--heater-export", type=Path)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    cases = input_cases() if not args.input else json.loads(args.input.read_text())
    if args.write_evidence and args.input:
        parser.error("Evidence must use the full deterministic case set")
    if args.prepare_input:
        args.prepare_input.parent.mkdir(parents=True, exist_ok=True)
        args.prepare_input.write_text(json.dumps(cases, separators=(",", ":")) + "\n")
        print(f"Prepared {len(cases)} cases and {sum(len(item['actions']) for item in cases)} actions")
    if not args.original_jsonl:
        if not args.prepare_input:
            parser.error("use --prepare-input or --original-jsonl")
        return 0
    if digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != IMAGE_SHA:
        raise SystemExit("Original image identity mismatch")
    execution = json.loads(Path(str(args.original_jsonl) + ".json").read_text())
    if (execution["input_sha256"] != IMAGE_SHA or execution["cases"] != len(cases) or
            execution["actions"] != sum(len(item["actions"]) for item in cases)):
        raise SystemExit("Unexpected original execution provenance")
    original = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if len(original) != len(cases):
        raise SystemExit("Original case count mismatch")
    counts = collections.Counter()
    exact, comparisons, maximum_error, goldens = 0, 0, 0., []
    with tempfile.TemporaryDirectory(prefix="zseries-heater-") as directory:
        native, compiler = load_native(directory)
        for item, expected in zip(cases, original):
            bit_matches, total, error = compare_case(native, item, expected)
            exact += bit_matches; comparisons += total; maximum_error = max(maximum_error, error)
            counts.update(action["kind"] for action in item["actions"])
            if not item["id"].startswith("random-") or int(item["id"].split("-")[1]) < 16:
                goldens.append({"input": item, "snapshots": expected["snapshots"]})
    report = {
        "schema_version": 1, "input_sha256": IMAGE_SHA, "cases": len(cases), "actions": sum(counts.values()),
        "scope": execution["scope"], "all_results_match": True, "action_counts": dict(counts),
        "binary64_comparisons": comparisons, "exact_binary64_matches": exact, "maximum_absolute_error": maximum_error,
        "original_functions_executed": execution["original_functions_executed"], "modeled_boundaries": execution["modeled_boundaries"],
        "original_execution_jsonl_sha256": digest(args.original_jsonl),
        "original_execution_report_sha256": digest(str(args.original_jsonl) + ".json"),
        "native_compiler": compiler, "native_flags": FLAGS,
        "portable_golden_cases": len(goldens), "portable_golden_actions": sum(len(x["input"]["actions"]) for x in goldens),
        "full_cpu_or_hardware_emulation": False, "hardware_tested": False,
    }
    if args.write_evidence:
        if not args.export or not args.heater_export:
            parser.error("--write-evidence requires --export and --heater-export")
        hashes = {}
        for address in execution["original_functions_executed"]:
            filename = "fn_" + address[2:]
            path = args.export / "instructions" / (filename + ".json")
            if not path.exists():
                path = args.heater_export / (filename + ".instructions.json")
            records = json.loads(path.read_text())
            if isinstance(records, dict):
                records = records["instructions"]
            if any(record["mnemonic"].lower().startswith("loop") for record in records):
                raise SystemExit("Unmodeled Xtensa hardware loop")
            hashes[address] = digest(path)
        report["instruction_export_sha256"] = hashes
        report["source_sha256"] = {path: digest(ROOT / path) for path in (
            "include/zseries/heater.hpp", "src/heater.cpp", "tests/heater_bridge.cpp",
            "tools/validate_heater.py", "tools/ghidra/EmulateHeater.java")}
        fixture = ROOT / "tests/fixtures/heater-original.json"
        fixture.write_text(json.dumps({"schema_version": 1, "input_sha256": IMAGE_SHA, "values": VALUE_NAMES,
                                      "words": WORD_NAMES, "cases": goldens}, separators=(",", ":")) + "\n")
        report["portable_golden_sha256"] = digest(fixture)
        (ROOT / "analysis/heater-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("cases", "actions", "all_results_match", "binary64_comparisons",
                     "exact_binary64_matches", "maximum_absolute_error", "portable_golden_cases")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
