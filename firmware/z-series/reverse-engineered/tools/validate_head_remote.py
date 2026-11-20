#!/usr/bin/env python3
"""Accept native Head remote session bodies only after comparison with original instructions."""
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
import head_remote_cases as inputs
import head_remote_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    fixture = args.original.resolve() / "head-remote-original.json"
    proof = json.loads((args.original / "head-remote-validation.json").read_text())
    original = json.loads(fixture.read_text())
    image_sha = common.shared.digest(common.INPUT)
    if (original["input_sha256"] != image_sha or proof["input_sha256"] != image_sha or
            proof["fixture_sha256"] != common.shared.digest(fixture)):
        raise ValueError("Original Head remote session fixture identity differs")
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
        raise ValueError("Original Head remote session case coverage differs")
    with tempfile.TemporaryDirectory(prefix="z119-head-remote-validation-") as directory:
        compiled = native.load_native(directory)
        for case, expected in zip(cases, original["cases"]):
            actual = native.run(compiled, case)
            if actual != native.compared(expected) or expected["use_azure"] != 0:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    proof["validated_source_sha256"] = {path: common.shared.digest(ROOT/path) for path in [
        "include/zseries/head_remote_session.hpp","src/head_remote_session.cpp","include/zseries/quantity.hpp","src/quantity.cpp",
        "include/zseries/enums.hpp","src/enums.cpp",
        "include/zseries/head_ui_commands.hpp","include/zseries/head_network_events.hpp","include/zseries/azure.hpp",
        "tests/head_remote_bridge.cpp","tests/head_remote_native.py","tests/test_head_remote.py","tools/validate_head_remote.py"]}
    proof["native_validation"] = dict(all_results_match=True,cases=len(cases),scope="Owned Core/Head payloads, quiet-period wraparound, returning assertions with exact diagnostic text/count/delay, and discard routing")
    proof["remaining_scope"] = [
        "Original Core remote-session body401021b8 executes complete construction, quantity conversion, assertions and final use_azure clearing.",
        "Head Azure/Core mailbox puts and Azure discard are explicit callback boundaries; the actual application owns these one-slot queues and clears Core.session.use_azure afterward.",
        "ROM memory/string functions, compiler arithmetic and prescribed clock/log/delay remain named boundaries. This is not physical-network, scheduler or installed-firmware evidence."]
    if args.write_evidence:
        destination = ROOT / "tests/fixtures/head-remote-original.json.gz"
        buffer=io.BytesIO()
        with gzip.GzipFile(filename="",mode="wb",fileobj=buffer,mtime=0) as output:output.write(fixture.read_bytes())
        destination.write_bytes(buffer.getvalue())
        proof["compressed_fixture_sha256"]=common.shared.digest(destination)
        proof["fixture_encoding"]="gzip with mtime=0; decompressed bytes retain fixture_sha256"
        (ROOT / "analysis/head-remote-validation.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(f"Verified {len(cases)} original Head remote session cases")


if __name__ == "__main__":
    main()
