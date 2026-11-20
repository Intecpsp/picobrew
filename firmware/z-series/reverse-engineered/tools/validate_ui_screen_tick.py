#!/usr/bin/env python3
"""Accept native ScreenLibrary tick bodies only after comparison with original instructions."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import ui_screen_tick_cases as inputs
import ui_screen_tick_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "ui-screen-tick-original.json"
    proof = json.loads((args.original / "ui-screen-tick-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original ScreenLibrary tick fixture identity differs")
    for path, expected in proof["generator_files"].items():
        if common.shared.digest(ROOT / path) != expected:
            raise ValueError("Original execution generator differs: " + path)
    raw = common.INPUT.read_bytes()
    parsed = common.images.parse_image(raw)
    segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                for s in parsed["segments"]]
    for function in original["original_functions"]:
        for span in function["ranges"]:
            if hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest() != span["sha256"]:
                raise ValueError("Executed original function bytes differ")
    cases = inputs.cases()
    if len(cases) != len(original["cases"]) or len(cases) != proof["cases"]:
        raise ValueError("Original ScreenLibrary tick case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-ui-screen-tick-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected):
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/ui_screen_tick.hpp", "src/ui_screen_tick.cpp",
        "tests/ui_screen_tick_bridge.cpp", "tests/ui_screen_tick_native.py", "tests/test_ui_screen_tick.py",
        "tools/validate_ui_screen_tick.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Exact periodic state, waiting frames, timer deadlines and ordered widget calls for normal and combined layouts")
    proof["remaining_scope"] = [
        "All drawing, menu text-scroll and slideshow operations are required services, with no default successful no-op.",
        "Pause, runtime-transient and notification drawing services own their internal deadline updates; this comparison validates their dispatch boundary.",
        "A zero active waiting-animation frame count returns InvalidFrameCount before division.",
        "A complete ScreenLibrary, linked application and physical display remain unverified."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/ui-screen-tick-original.json"
        if fixture != destination:
            shutil.copyfile(fixture, destination)
        (ROOT / "analysis/ui-screen-tick-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original ScreenLibrary tick cases")


if __name__ == "__main__":
    main()
