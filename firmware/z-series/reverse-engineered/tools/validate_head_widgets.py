#!/usr/bin/env python3
"""Accept native Head widget framebuffer bodies only after comparison with original instructions."""
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
import head_widgets_cases as inputs
import head_widgets_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-widgets-original.json"
    proof = json.loads((args.original / "head-widgets-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head widget framebuffer fixture identity differs")
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
        raise ValueError("Original Head widget framebuffer case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-widgets-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected):
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_widgets.hpp", "src/head_widgets.cpp", "include/zseries/display.hpp", "src/display.cpp",
        "include/zseries/display_text.hpp", "src/display_text.cpp", "src/display_fonts.cpp",
        "include/zseries/enums.hpp", "src/enums.cpp", "include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp",
        "tests/head_widgets_bridge.cpp", "tests/head_widgets_native.py", "tests/test_head_widgets.py",
        "tools/validate_head_widgets.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Full 4096-byte framebuffer hashes, dirty flag, assertion counter/line and title height for eight concrete widget painters")
    proof["remaining_scope"] = [
        "Original widget instructions execute through actual text, rectangle, font and bitmap bodies using constructor-established field defaults.",
        "Original assertion bodies log, increment, delay 200 ms and return; comparisons include the continuation.",
        "Windowed call/return and MOVSP stack changes are explicit ABI adaptations; processor window spill/underflow exceptions are not emulated.",
        "ROM memory/string functions, bounded snprintf, prescribed clock/log/delay and compiler arithmetic are explicit boundaries.",
        "The source uses the bounded display UTF-8 decoder; malformed strings, title prefixes splitting a multibyte character and invalid intersections from undersized duration regions remain explicit rejection cases outside this valid-text evidence.",
        "Screen ownership, menu animation, dynamic runtime/slideshow views and physical panel/RTOS behavior are outside this painter comparison."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-widgets-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-widgets-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head widget framebuffer cases")


if __name__ == "__main__":
    main()
