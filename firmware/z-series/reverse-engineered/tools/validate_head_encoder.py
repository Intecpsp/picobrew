#!/usr/bin/env python3
"""Accept native Head encoder bodies only after comparison with original instructions."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import head_encoder_cases as inputs
import head_encoder_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-encoder-original.json"
    proof = json.loads((args.original / "head-encoder-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head encoder fixture identity differs")
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
        raise ValueError("Original Head encoder case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-encoder-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected) or expected["event_pending"] != 0:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_encoder.hpp", "src/head_encoder.cpp", "include/zseries/head_flow.hpp", "src/head_flow.cpp",
        "tests/head_encoder_bridge.cpp", "tests/head_encoder_native.py", "tests/test_head_encoder.py",
        "tools/validate_head_encoder.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Exact widget state, dirty flag, timer deadlines, assertion boundaries and ordered draw/select/clock operations after the encoder event is consumed")
    proof["remaining_scope"] = [
        "Drawing, slideshow selection, combined paragraph/menu selection and diagnostic paragraph actions are required services, with no default successful no-op.",
        "The source handler API accepts an already consumed event; original one-slot mailbox consumption executes in the oracle and is separately retained as evidence.",
        "Zero delta and a negative character index after the original single-wrap correction return typed errors with the original assertion lines. A zero slideshow count returns InvalidState before division.",
        "Physical display/actuator behavior, concurrent event delivery and a complete linked application remain unverified."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-encoder-original.json"
        if fixture != destination:
            shutil.copyfile(fixture, destination)
        (ROOT / "analysis/head-encoder-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head encoder cases")


if __name__ == "__main__":
    main()
