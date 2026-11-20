#!/usr/bin/env python3
"""Accept native Head Core/Still bodies only after comparison with original instructions."""
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
import head_core_cases as inputs
import head_core_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-core-original.json"
    proof = json.loads((args.original / "head-core-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head Core/Still fixture identity differs")
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
        raise ValueError("Original Head Core/Still case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-core-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected) or expected["event_pending"] != 0:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT / path) for path in [
        "include/zseries/head_core_events.hpp", "src/head_core_events.cpp", "include/zseries/head_flow.hpp", "include/zseries/core_flow.hpp",
        "include/zseries/program_list.hpp", "include/zseries/program_repository.hpp", "include/zseries/enums.hpp", "src/enums.cpp",
        "include/zseries/ui_catalog.hpp", "src/ui_catalog.cpp", "tests/core_event_fixture.hpp",
        "tests/head_core_bridge.cpp", "tests/head_core_native.py", "tests/test_head_core.py",
        "tools/validate_head_core.py"]}
    proof["native_validation"] = dict(all_results_match=True, cases=len(cases),
        scope="Exact timer/progress/display state, owned event payload, returning-assertion counter, localized text and ordered Core event/state presentation operations; Still mailbox retained values")
    proof["remaining_scope"] = [
        "Presentation helper bodies, Core command delivery and the virtual maintenance query remain required services.",
        "Both pending and empty reads execute the original mailbox consumer, which clears pending and retains its event value.",
        "Progress above 100 and zero power-prompt duration execute the real returning assertion, counter increment and prescribed 200 ms delay before continuation.",
        "Clock, delay, log, ROM copying and compiler arithmetic are named modeled boundaries; no full CPU/RTOS/hardware claim is made.",
        "Full Head/widget/application assembly and physical panel/device operation remain unverified."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-core-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:
            output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"] = common.shared.digest(destination)
        proof["fixture_encoding"] = "gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-core-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head Core/Still cases")


if __name__ == "__main__":
    main()
