#!/usr/bin/env python3
"""Accept native Head owned-screen framebuffer bodies only after comparison with original instructions."""
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
import head_screens_cases as inputs
import head_screens_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-screens-original.json"
    proof = json.loads((args.original / "head-screens-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head owned-screen framebuffer fixture identity differs")
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
        raise ValueError("Original Head owned-screen framebuffer case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-screens-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected):
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_screens.hpp", "src/head_screens.cpp", "include/zseries/head_hardware.hpp", "src/head_hardware.cpp", "include/zseries/head_still_display.hpp", "src/head_still_display.cpp", "include/zseries/head_slideshow.hpp", "src/head_slideshow.cpp", "include/zseries/head_presentation.hpp", "src/head_presentation.cpp", "include/zseries/head_character.hpp", "src/head_character.cpp", "include/zseries/head_quantity.hpp", "src/head_quantity.cpp", "include/zseries/quantity.hpp", "src/quantity.cpp", "include/zseries/head_dynamic.hpp", "src/head_dynamic.cpp", "include/zseries/head_widgets.hpp", "src/head_widgets.cpp", "include/zseries/head_menu.hpp", "src/head_menu.cpp", "include/zseries/head_paragraph.hpp", "src/head_paragraph.cpp", "include/zseries/ui_screen_tick.hpp",
        "include/zseries/head_encoder.hpp", "src/head_encoder.cpp", "include/zseries/head_flow.hpp", "src/head_flow.cpp", "src/ui_screen_tick.cpp", "include/zseries/head_ui_commands.hpp", "include/zseries/display_effects.hpp", "src/display_effects.cpp", "include/zseries/display.hpp", "src/display.cpp",
        "include/zseries/display_text.hpp", "src/display_text.cpp", "src/display_fonts.cpp",
        "include/zseries/enums.hpp", "src/enums.cpp", "include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp",
        "tests/head_screens_bridge.cpp", "tests/head_screens_native.py", "tests/test_head_screens.py",
        "tools/validate_head_screens.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Full framebuffer hashes, composed widget state/timers, owned menu/text contents, input/quantity editors, recurring screens and diagnostic paragraph wrapping")
    proof["remaining_scope"] = [
        "Actual UiManagement layout/reset and Head home, instruction, waiting and registration bodies execute with actual glyph/bitmap/widget drawing.",
        "Covered layouts additionally include character/quantity editors, RuntimeState, RuntimeInfo, Pause and Level; all22 diagnostic paragraph examples and navigation clamps execute.",
        "Hardware/Still/slideshow layouts are connected to concrete painters and covered by the native Head lifecycle integration; their renderer bodies have separate original comparisons, beyond this335-case fixture.",
        "The native source owns 32 menu payloads and32 paragraph lines; encoder/periodic operations share live state. RuntimeInfo refresh500ms comes from constructor400f36b8; reset preserves font/spacing and restores full display text bounds.",
        "The original waiting counter is loaded unsigned even though decompiler type inference calls it short; value 65535 is retained.",
        "Original TRUNC.S finite signed conversions use an explicit 2^scale boundary to correct Ghidra 12.2's raw integer-scale float reinterpretation. Nonfinite and overflowing conversions are outside the oracle domain.",
        "ROM primitives, compiler arithmetic, prescribed clock, diagnostic log/delay and windowed call/stack adaptation remain named execution boundaries.",
        "Invalid/malformed or byte-truncated UTF-8, invalid rectangles and excess owned data are bounded native errors; whole application and physical display behavior require separate evidence."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-screens-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-screens-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head owned-screen framebuffer cases")


if __name__ == "__main__":
    main()
