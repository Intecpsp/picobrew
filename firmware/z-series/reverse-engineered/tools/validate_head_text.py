#!/usr/bin/env python3
"""Accept native Head text input bodies only after comparison with original instructions."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import head_text_cases as inputs
import head_text_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-text-original.json"
    proof = json.loads((args.original / "head-text-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head text input fixture identity differs")
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
        raise ValueError("Original Head text input case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-text-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected) or expected["event_pending"] != 0:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_text_input.hpp", "src/head_text_input.cpp", "include/zseries/enums.hpp",
        "tests/head_text_bridge.cpp", "tests/head_text_native.py", "tests/test_head_text.py",
        "tools/validate_head_text.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Exact password/kit storage, selected command fields, input flag and ordered command/synchronization operations after event consumption")
    proof["remaining_scope"] = [
        "Typed HeadTextCommand envelopes require a concrete Core assembly/post service; they do not fabricate missing CoreCommand payload fields.",
        "ROM strlen/memory operations and atoi are explicit oracle boundaries. Decimal cases are limited to representable signed32 results; overflow equivalence is not claimed.",
        "Unterminated65-byte input returns UnterminatedText before any state/action changes.",
        "The source accepts an already consumed event; original mailbox clearing is separately retained.",
        "Complete Head assembly, linked application and physical operation remain unverified."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-text-original.json"
        if fixture != destination:
            shutil.copyfile(fixture, destination)
        (ROOT / "analysis/head-text-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head text input cases")


if __name__ == "__main__":
    main()
