#!/usr/bin/env python3
"""Accept independently executed original Runtime mailbox evidence."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import run_msp_emulation as common
import runtime_events_cases as inputs
import runtime_events_native as native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original-directory", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original_directory / "runtime-events-original.json"
    report = json.loads((args.original_directory / "runtime-events-validation.json").read_text())
    original = json.loads(fixture.read_text())
    cases = inputs.cases()
    if report["fixture_sha256"] != common.shared.digest(fixture):
        raise ValueError("Original fixture differs from its execution report")
    if report["input_sha256"] != common.shared.digest(common.INPUT) or len(original["cases"]) != len(cases):
        raise ValueError("Original image or execution count differs")
    for path, expected in report["generator_files"].items():
        if common.shared.digest(ROOT / path) != expected:
            raise ValueError("Original generator differs: " + path)
    with tempfile.TemporaryDirectory(prefix="runtime-events-validation-") as directory:
        library = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            if native.run(library, case) != expected:
                raise ValueError("Runtime event payload mismatch: " + str(case))
    report["native_validation"] = dict(all_results_match=True, mailbox_cases=len(cases),
        scope="Four independent Runtime event fields, drop-new mailbox ownership, and Head notification adaptation", complete_application_linked=False)
    files = ["include/zseries/core_flow.hpp", "include/zseries/runtime_events.hpp", "src/runtime_events.cpp",
             "tests/runtime_events_bridge.cpp", "tests/runtime_events_native.py", "tests/test_runtime_events.py", "tools/validate_runtime_events.py"]
    report["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in files}
    if args.write_evidence:
        shutil.copyfile(fixture, ROOT / "tests/fixtures/runtime-events-original.json")
        (ROOT / "analysis/runtime-events-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"All {len(cases)} original Runtime event mailbox cases match")


if __name__ == "__main__":
    main()
