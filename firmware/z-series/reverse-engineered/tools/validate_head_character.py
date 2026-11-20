#!/usr/bin/env python3
"""Accept native Head character-picker framebuffer bodies only after comparison with original instructions."""
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
import head_character_cases as inputs
import head_character_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-character-original.json"
    proof = json.loads((args.original / "head-character-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head character-picker framebuffer fixture identity differs")
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
        raise ValueError("Original Head character-picker framebuffer case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-character-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected):
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_character.hpp", "src/head_character.cpp", "include/zseries/head_widgets.hpp", "src/head_widgets.cpp", "include/zseries/head_encoder.hpp",
        "include/zseries/display_effects.hpp", "src/display_effects.cpp", "include/zseries/display.hpp", "src/display.cpp",
        "include/zseries/display_text.hpp", "src/display_text.cpp", "src/display_fonts.cpp",
        "include/zseries/enums.hpp", "src/enums.cpp", "include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp",
        "tests/head_character_bridge.cpp", "tests/head_character_native.py", "tests/test_head_character.py", "tools/validate_head_character.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Complete 4096-byte framebuffer hashes, selected/enabled state, editable text/capacity, dirty state and emitted event copies")
    proof["remaining_scope"] = [
        "Original draw and button bodies execute, including actual string-buffer mutation, UTF-8 font drawing, edge gradients and rounded selection.",
        "RFID acceptance requires exactly fourteen characters; cancel clears and disables input; text space bypasses the normal forty-two-character gate but retains the sixty-four-byte buffer capacity.",
        "Constructor/input setup fields, external copied-event sinks, ROM primitives, compiler arithmetic, prescribed clock/log/delay and windowed ABI are explicit boundaries.",
        "UiManagement layout selection and full Head integration require separate evidence. Invalid geometry and invalid selected indices are bounded native errors.",
        "This does not establish physical display operation or a complete installed firmware image."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-character-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-character-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head character-picker framebuffer cases")


if __name__ == "__main__":
    main()
