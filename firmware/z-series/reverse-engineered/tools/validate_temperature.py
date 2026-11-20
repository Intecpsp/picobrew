#!/usr/bin/env python3
"""Compare compiled reconstruction to an original-instruction execution CSV.

Run EmulateTemperature.java first on the analyzed, byte-verified Z119 image.
This tool requires every ADC code 0..4095 for all four original thermistors.
It creates a compact validation report and representative original-execution
goldens only when explicitly given --write-evidence.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INPUT_SHA256 = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"
REPRESENTATIVE_CODES = {0, 1, 14, 15, 16, 100, 250, 512, 1024, 1536, 2048,
                        2560, 3072, 3584, 4000, 4093, 4094, 4095}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--export", type=Path, required=True,
                        help="Preserved Ghidra export with instruction metadata")
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    image = ROOT.parent / "oem/upstream/zseries_0_0_119.bin"
    if sha256(image) != INPUT_SHA256:
        raise SystemExit("Preserved input identity mismatch")
    execution = json.loads(Path(str(args.csv) + ".json").read_text())
    if (execution["input_sha256"] != INPUT_SHA256 or execution["cases"] != 16384 or
            execution["function"] != "0x400d6ac0"):
        raise SystemExit("Unexpected original execution provenance or extent")
    instruction_path = args.export / "instructions/fn_400d6ac0.json"
    instructions = json.loads(instruction_path.read_text())
    if any(x["mnemonic"].lower().startswith("loop") for x in instructions):
        raise SystemExit("Xtensa LOOP requires an independently checked emulator model")
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = ["-std=c++11", "-O2", "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared"]
    with tempfile.TemporaryDirectory(prefix="zseries-temperature-") as directory:
        build = Path(directory)
        bridge = build / "bridge.cpp"
        bridge.write_text('''#include "zseries/temperature.hpp"
extern "C" double reconstructed(unsigned sensor, int raw) {
    using namespace zseries::oem;
    return thermistor_kelvin(*thermistor_parameters(static_cast<TempSensorType>(sensor)), raw);
}
''')
        library = build / "temperature.so"
        subprocess.run(compiler + flags + ["-I", str(ROOT / "include"), str(bridge),
                       str(ROOT / "src/temperature.cpp"), "-o", str(library)], check=True)
        native = ctypes.CDLL(str(library)).reconstructed
        native.argtypes = [ctypes.c_uint, ctypes.c_int]
        native.restype = ctypes.c_double
        seen = set()
        goldens = []
        maximum_absolute_error = 0.0
        bit_identical = 0
        for row in csv.DictReader(args.csv.open()):
            sensor, raw = int(row["sensor_type"]), int(row["raw_adc"])
            if not 0 <= sensor < 4 or not 0 <= raw <= 4095 or (sensor, raw) in seen:
                raise SystemExit("Duplicate or out-of-domain original execution row")
            seen.add((sensor, raw))
            expected, actual = float(row["kelvin"]), native(sensor, raw)
            error = abs(actual - expected)
            if not math.isfinite(expected) or not math.isfinite(actual) or error > 2e-12:
                raise SystemExit(f"Original/reconstruction mismatch at sensor={sensor} raw={raw}: {expected} != {actual}")
            maximum_absolute_error = max(maximum_absolute_error, error)
            bit_identical += actual.hex() == expected.hex()
            if raw in REPRESENTATIVE_CODES:
                goldens.append({"sensor_type": sensor, "raw_adc": raw, "kelvin": expected})
        if len(seen) != 16384:
            raise SystemExit("Missing original execution rows")

    report = {
        "schema_version": 1,
        "input_sha256": INPUT_SHA256,
        "function": "0x400d6ac0",
        "scope": execution["scope"],
        "sdk_boundary_implementation": "Java binary64 arithmetic and Math.log; diagnostic calls modeled separately",
        "instruction_count": len(instructions),
        "hardware_loop_instructions": 0,
        "cases": len(seen),
        "sensor_type_domain": [0, 3],
        "adc_domain": [0, 4095],
        "native_compiler": subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0],
        "native_flags": flags,
        "maximum_absolute_kelvin_error": maximum_absolute_error,
        "bit_identical_to_hooked_execution_cases": bit_identical,
        "tolerance_kelvin": 2e-12,
        "execution_csv_sha256": sha256(args.csv),
        "execution_report_sha256": sha256(Path(str(args.csv) + ".json")),
        "instruction_export_sha256": sha256(instruction_path),
        "source_sha256": {
            path: sha256(ROOT / path) for path in (
                "include/zseries/temperature.hpp", "src/temperature.cpp",
                "tools/ghidra/EmulateTemperature.java", "tools/validate_temperature.py")
        },
        "sdk_hook_calls": execution["sdk_hook_calls"],
        "full_cpu_or_hardware_emulation": False,
        "exact_original_libm_execution": False,
        "hardware_tested": False,
    }
    if args.write_evidence:
        fixture = {
            "schema_version": 1, "input_sha256": INPUT_SHA256, "function": "0x400d6ac0",
            "full_execution_cases": len(seen), "execution_csv_sha256": sha256(args.csv),
            "scope": execution["scope"], "cases": goldens,
        }
        fixtures = ROOT / "tests/fixtures"
        fixtures.mkdir(parents=True, exist_ok=True)
        (fixtures / "temperature-original.json").write_text(json.dumps(fixture, indent=2) + "\n")
        (ROOT / "analysis/temperature-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in (
        "cases", "maximum_absolute_kelvin_error", "bit_identical_to_hooked_execution_cases",
        "hardware_tested")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
