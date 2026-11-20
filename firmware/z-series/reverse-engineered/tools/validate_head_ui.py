#!/usr/bin/env python3
"""Accept native Head UI command bodies only after comparison with original instructions."""
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
import head_ui_cases as inputs
import head_ui_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-ui-original.json"
    proof = json.loads((args.original / "head-ui-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head UI command fixture identity differs")
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
        raise ValueError("Original Head UI command case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-ui-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected) or expected["state"][8] != 0:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_ui_commands.hpp", "src/head_ui_commands.cpp", "include/zseries/head_flow.hpp",
        "include/zseries/head_numeric_inputs.hpp", "include/zseries/head_text_input.hpp", "include/zseries/core_flow.hpp",
        "include/zseries/program_list.hpp", "include/zseries/program_repository.hpp", "include/zseries/program_runtime.hpp",
        "include/zseries/enums.hpp", "src/enums.cpp", "include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp",
        "tests/core_event_fixture.hpp", "tests/head_ui_fixture.hpp", "tests/head_ui_bridge.cpp", "tests/head_ui_native.py",
        "tests/test_head_ui.py", "tools/validate_head_ui.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="All 176 UI commands and invalid IDs; ordered effects, localized titles, static menu items, complete typed Core payloads, recipe/editor/search/timer state and returning assertions")
    proof["remaining_scope"] = [
        "Named screen/helper bodies, Head Core dispatch, alarm, synchronization and Core mailbox delivery remain required integration services.",
        "The source owns Core detail storage through HeadUiCoreEnvelope; each mailbox copy rebinds detail to its destination's payload.",
        "Missing index/recipe flags and non-coffee program assertions execute and return before original payload access and Core posting; zero timer duration also continues.",
        "Clock, delay, log, ROM copying and compiler arithmetic are named modeled boundaries; no full CPU/RTOS/hardware claim is made.",
        "Concrete widget rendering and physical front-panel/application behavior require their own evidence."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-ui-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-ui-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head UI command cases")


if __name__ == "__main__":
    main()
