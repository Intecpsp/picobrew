#!/usr/bin/env python3
"""Accept native Head network event bodies only after comparison with original instructions."""
import argparse
import hashlib
import gzip
import io
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import head_network_cases as inputs
import head_network_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-network-original.json"
    proof = json.loads((args.original / "head-network-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head network event fixture identity differs")
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
        raise ValueError("Original Head network event case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-network-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected) or expected["event_pending"] != 0:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_network_events.hpp", "src/head_network_events.cpp", "include/zseries/enums.hpp", "src/enums.cpp",
        "include/zseries/quantity.hpp", "src/quantity.cpp", "include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp",
        "tests/head_network_bridge.cpp", "tests/head_network_native.py", "tests/test_head_network.py",
        "tools/validate_head_network.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Exact retained/normalized program parameter bits, assertions, translated titles, menu choices and ordered Wi-Fi/Azure presentation/command operations")
    proof["remaining_scope"] = [
        "Screen reset/append/presentation, home, Wi-Fi scan-result generation and Core delivery remain required services.",
        "The source handler API accepts consumed events; original mailbox clearing executes separately in the oracle.",
        "Incompatible coffee quantities return InvalidQuantity and retain the original assertion line after preceding state/effects.",
        "Complete Head/Core assembly, linked application and physical device operation remain unverified."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-network-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-network-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head network event cases")


if __name__ == "__main__":
    main()
