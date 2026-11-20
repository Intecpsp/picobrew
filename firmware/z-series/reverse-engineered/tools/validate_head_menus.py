#!/usr/bin/env python3
"""Accept native Head application-menu framebuffer bodies only after comparison with original instructions."""
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
import head_menus_cases as inputs
import head_menus_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-menus-original.json"
    proof = json.loads((args.original / "head-menus-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head application-menu framebuffer fixture identity differs")
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
        raise ValueError("Original Head application-menu framebuffer case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-menus-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected):
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    sources=sorted((ROOT/'include/zseries').glob('head_*.hpp'))+sorted((ROOT/'src').glob('head_*.cpp'))
    sources += [ROOT/path for path in ['include/zseries/display.hpp','src/display.cpp','include/zseries/display_text.hpp','src/display_text.cpp','src/display_fonts.cpp','src/display_effects.cpp','include/zseries/display_effects.hpp','include/zseries/enums.hpp','src/enums.cpp','include/zseries/ui_catalog.hpp','src/ui_catalog.cpp','include/zseries/ui_screen_tick.hpp','src/ui_screen_tick.cpp','include/zseries/quantity.hpp','src/quantity.cpp','tests/head_menus_bridge.cpp','tests/head_menus_native.py','tests/test_head_menus.py','tools/validate_head_menus.py']]
    proof["validated_source_sha256"]={str(path.relative_to(ROOT)):common.shared.digest(path)for path in sources}
    proof["native_validation"]=dict(all_results_match=True,cases=len(cases),scope="Complete4096-byte framebuffers, shared widget state, menu commands/labels, paragraph contents and returning diagnostics through original Head UI dispatch and presentation bodies")
    proof["remaining_scope"]=[
        "All167 application-menu cases execute original UI dispatcher401080d4 through Head menu/presentation helpers, UiManagement40102d30 and actual graphics primitives.",
        "The fixture uses the actual embedded Head Display alias so diagnostic helpers and normal painters address the same framebuffer and text context.",
        "Alarm servicing, SDK WiFi/IP/String/flash results, constructor fields, ROM primitives, compiler arithmetic, prescribed clock/log/delay and windowed ABI remain explicit boundaries.",
        "The runner raises only the interpreted nesting guard16to32 for this deeper application chain; shared DisplayExecution instructions are unchanged. MOVSP and finite TRUNC.S retain their documented boundaries.",
        "Native owner integration additionally tests mailbox priority/drop-new, copied payload lifetimes, input round trips, shared console dirty state and connected periodic painters; these are application tests rather than a full scheduler/hardware replay.",
        "Runtime/Still/recipe/error presentations beyond the selected menus are implemented from original bodies; their children have separate original comparisons. This fixture does not establish every application path or installed firmware behavior."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-menus-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-menus-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head application-menu framebuffer cases")


if __name__ == "__main__":
    main()
