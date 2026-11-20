#!/usr/bin/env python3
"""Accept native Head numeric input bodies only after comparison with original instructions."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import head_numeric_cases as inputs
import head_numeric_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-numeric-original.json"
    proof = json.loads((args.original / "head-numeric-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head numeric input fixture identity differs")
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
        raise ValueError("Original Head numeric input case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-numeric-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected) or expected["event_pending"] != 0:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_numeric_inputs.hpp", "src/head_numeric_inputs.cpp", "include/zseries/enums.hpp",
        "tests/head_numeric_bridge.cpp", "tests/head_numeric_native.py", "tests/test_head_numeric.py",
        "tools/validate_head_numeric.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Exact input bits, CoreCommand payload fields, parameter updates, flags, assertion line/counter, ordered assertion/delay and subsequent screen operations after the event is consumed")
    proof["remaining_scope"] = [
        "Core command delivery and screen presentation are required services, with no default successful no-op.",
        "The source handler API accepts an already consumed event; original one-slot mailbox consumption executes in the oracle and is separately retained as evidence.",
        "Invalid quantity IDs execute the returning diagnostic callback and continue to the unchanged manual or coffee editor, matching the original helper and continuation.",
        "Physical display/actuator behavior, concurrent event delivery and a complete linked application remain unverified."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-numeric-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"]=common.shared.digest(destination)
        proof["fixture_encoding"]="gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-numeric-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head numeric input cases")


if __name__ == "__main__":
    main()
