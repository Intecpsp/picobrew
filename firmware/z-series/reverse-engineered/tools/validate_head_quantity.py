#!/usr/bin/env python3
"""Accept native Head quantity-editor framebuffer bodies only after comparison with original instructions."""
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
import head_quantity_cases as inputs
import head_quantity_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-quantity-original.json"
    proof = json.loads((args.original / "head-quantity-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head quantity-editor framebuffer fixture identity differs")
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
        raise ValueError("Original Head quantity-editor framebuffer case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-quantity-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected):
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_quantity.hpp", "src/head_quantity.cpp", "include/zseries/quantity.hpp", "src/quantity.cpp",
        "include/zseries/head_widgets.hpp", "src/head_widgets.cpp", "include/zseries/head_encoder.hpp", "include/zseries/head_numeric_inputs.hpp",
        "include/zseries/display.hpp", "src/display.cpp", "include/zseries/display_text.hpp", "src/display_text.cpp", "src/display_fonts.cpp",
        "include/zseries/enums.hpp", "src/enums.cpp", "include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp",
        "tests/head_quantity_bridge.cpp", "tests/head_quantity_native.py", "tests/test_head_quantity.py", "tools/validate_head_quantity.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Complete 4096-byte framebuffer hashes, metric/unit/id, enabled/wrap/selection/count, minimum/step, dirty state, assertion line/counter and 186 unit-formatting results")
    proof["remaining_scope"] = [
        "Original quantity initialization, discrete selection, all four painter families and unit formatter bodies execute, including actual returning assertion helper and clamp continuation.",
        "The temperature secondary value uses the actual fifth formatter argument (precision one), which the decompiler omitted.",
        "Constructor fields, ROM primitives, compiler arithmetic, fixed-format bounded snprintf, prescribed clock/log/delay and windowed ABI are explicit boundaries.",
        "snprintf implements only the reviewed fixed-point/string/integer formats with half-even decimal rounding; finite round and unsigned conversion have explicit supported bounds.",
        "Containing UiManagement and Head event integration, invalid geometry, nonfinite values and unsupported selector IDs require separate coverage.",
        "This does not establish physical display operation or a complete installed firmware image."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-quantity-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-quantity-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head quantity-editor framebuffer cases")


if __name__ == "__main__":
    main()
