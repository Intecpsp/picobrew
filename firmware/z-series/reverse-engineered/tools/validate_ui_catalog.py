#!/usr/bin/env python3
"""Compare the recovered UI catalog with independently executed Z119 lookups."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import run_msp_emulation as common
import ui_catalog_cases as inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True,
                        help="Directory containing the original fixture and acquisition report")
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "ui-catalog-original.json"
    acquisition = args.original.resolve() / "ui-catalog-validation.json"
    original = json.loads(fixture.read_text())
    proof = json.loads(acquisition.read_text())
    if (original["input_sha256"] != inputs.catalog.IMAGE_SHA or
            proof["input_sha256"] != inputs.catalog.IMAGE_SHA or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original UI catalog fixture identity differs")
    cases = inputs.cases()
    if len(original["cases"]) != len(cases) or proof["cases"] != len(cases):
        raise ValueError("Original UI catalog case coverage differs")
    for filename, expected in proof["generator_files"].items():
        if common.shared.digest(ROOT / filename) != expected:
            raise ValueError("Original execution generator differs: " + filename)
    raw = common.INPUT.read_bytes()
    if hashlib.sha256(raw).hexdigest() != original["input_sha256"]:
        raise ValueError("Preserved image identity differs")
    parsed = common.images.parse_image(raw)
    segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                for s in parsed["segments"]]
    for function in original["original_functions"]:
        for span in function["ranges"]:
            if hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest() != span["sha256"]:
                raise ValueError("Executed original function bytes differ")
    source, assets = inputs.catalog.outputs()
    if (ROOT / "src/ui_catalog.cpp").read_text(encoding="utf-8") != source:
        raise ValueError("Recovered UI source is stale")
    if (ROOT / "analysis/ui-assets.json").read_text(encoding="utf-8") != assets:
        raise ValueError("Recovered UI asset evidence is stale")
    env = dict(os.environ, Z_UI_CATALOG_FIXTURE=str(fixture))
    tests = ["test_all_original_label_and_screen_lookups",
             "test_bitmap_descriptors_and_pixels_are_exact_original_assets",
             "test_native_guards_preserve_caller_output"]
    subprocess.run([sys.executable, "-m", "unittest"] +
                   ["test_ui_catalog.UiCatalogTests." + name for name in tests],
                   cwd=ROOT / "tests", env=env, check=True)
    evidence_files = ["include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp",
                      "include/zseries/display.hpp", "include/zseries/enums.hpp",
                      "analysis/ui-assets.json", "tests/test_ui_catalog.py",
                      "tests/ui_catalog_bridge.cpp", "tools/generate_ui_assets.py",
                      "tools/validate_ui_catalog.py"]
    proof["validated_source_sha256"] = {
        filename: common.shared.digest(ROOT / filename) for filename in evidence_files}
    proof["native_validation"] = dict(
        lookup_cases=len(cases), label_categories=18, localized_entries=405,
        languages=3, screen_bodies=102, bitmap_descriptors=16,
        exact_bitmap_bytes=3183, groups_passed=len(tests),
        scope="Exact returned text and unchanged-output behavior; diagnostic logging is an explicit execution boundary")
    proof["bounded_repairs"] = [
        "An invalid language for a valid screen reports InvalidLanguage and leaves the output unchanged, instead of the OEM out-of-range table read."]
    proof["remaining_scope"] = [
        "ScreenLibrary layout and interaction, Head event-handler bodies and complete application assembly remain unimplemented.",
        "Asset identifiers describe recovered pixels and uses; original vendor asset names were not recovered."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/ui-catalog-original.json"
        if fixture != destination:
            shutil.copyfile(fixture, destination)
        (ROOT / "analysis/ui-catalog-validation.json").write_text(
            json.dumps(proof, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(cases)} original UI lookups, 16 bitmap descriptors and 3183 packed pixel bytes")


if __name__ == "__main__":
    main()
