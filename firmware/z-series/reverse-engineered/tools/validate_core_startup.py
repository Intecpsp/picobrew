#!/usr/bin/env python3
"""Compare recovered Core startup integration with original call-order traces."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"
FLAGS = ["-std=c++11", "-O2", "-fPIC", "-shared", "-fno-fast-math", "-ffp-contract=off"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_native(directory):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    library = Path(directory) / "core_startup.so"
    subprocess.run(compiler + FLAGS + ["-I", str(ROOT / "include"), str(ROOT / "tests/core_startup_bridge.cpp")] +
                   [str(ROOT / "src" / name) for name in ("core_startup.cpp", "heater.cpp", "sensor_acquisition.cpp", "temperature.cpp")] +
                   ["-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.core_startup_trace.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_int,
                                        ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
    native.core_startup_trace.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0]


def invoke(native, boiler=0, fail_stage=-1, missing=False):
    events, count = (ctypes.c_uint32 * 640)(), ctypes.c_uint32()
    result = native.core_startup_trace(boiler, fail_stage, missing, events, ctypes.byref(count))
    if count.value > 128:
        raise AssertionError("Native startup trace exceeded bound")
    return result, [list(events[index * 5:index * 5 + 5]) for index in range(count.value)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--export", type=Path)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    original = json.loads(args.original.read_text())
    if (original["input_sha256"] != IMAGE_SHA or original["function"] != "0x40113078" or
            digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != IMAGE_SHA or len(original["cases"]) != 4):
        raise SystemExit("Original startup identity mismatch")
    with tempfile.TemporaryDirectory(prefix="zseries-startup-") as directory:
        native, compiler = load_native(directory)
        for item in original["cases"]:
            result, events = invoke(native, item["boiler"], fail_stage=-1 if item["timer_start"] else -2)
            if result != 0 or events != item["events"]:
                raise AssertionError(f"Startup trace mismatch: boiler {item['boiler']}; native {events}; original {item['events']}")
    report = {"schema_version": 1, "input_sha256": IMAGE_SHA, "function": "0x40113078", "scope": original["scope"],
              "cases": 4, "all_results_match": True, "events_compared": sum(len(item["events"]) for item in original["cases"]),
              "native_compiler": compiler, "native_flags": FLAGS, "hardware_tested": False, "full_cpu_or_hardware_emulation": False}
    if args.write_evidence:
        if not args.export:
            parser.error("--write-evidence requires --export")
        report["instruction_export_sha256"] = digest(args.export / "instructions/fn_40113078.json")
        report["source_sha256"] = {path: digest(ROOT / path) for path in ("include/zseries/core_startup.hpp", "src/core_startup.cpp",
                                  "tests/core_startup_bridge.cpp", "tools/validate_core_startup.py", "tools/ghidra/EmulateCoreStartup.java", "tools/ghidra/Z119Execution.java")}
        fixture = ROOT / "tests/fixtures/core-startup-original.json"
        fixture.write_text(json.dumps(original, indent=2) + "\n")
        report["portable_golden_sha256"] = digest(fixture)
        (ROOT / "analysis/core-startup-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("cases", "events_compared", "all_results_match")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
