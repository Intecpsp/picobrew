#!/usr/bin/env python3
"""Accept native Head encoder bodies only after comparison with original instructions."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
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
    bounded=sum("bounded_guard" in case for case in original["cases"])
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        complete_original_matches=len(cases)-bounded, bounded_guard_prefix_matches=bounded,
        scope="Exact widget state, dirty flag, timer deadlines and ordered effects for completed original paths; negative-character guards match separately retained original prefixes, not OEM completion")
    proof["remaining_scope"] = [
        "Drawing, slideshow selection, combined paragraph/menu selection and diagnostic paragraph actions are required services, with no default successful no-op.",
        "The source handler API accepts an already consumed event; original one-slot mailbox consumption executes in the oracle and is separately retained as evidence.",
        "Zero delta retains the returning assertion and continues the complete widget/timer sequence. The original assertion helper executes through RETW; its 200 ms delay/log output use explicit diagnostic boundaries with prescribed clock samples.",
        "Negative character selection after the single-wrap correction is a deliberate typed-error repair before potentially invalid child drawing access. Its original completed continuation and the guarded prefix are both retained; these are not equivalent final outcomes. A zero slideshow count is a separate source-only division guard.",
        "Physical display/actuator behavior, concurrent event delivery and a complete linked application remain unverified."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-encoder-original.json.gz"
        buffer = io.BytesIO()
        with gzip.GzipFile(filename="", mode="wb", fileobj=buffer, mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-encoder-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head encoder cases")


if __name__ == "__main__":
    main()
