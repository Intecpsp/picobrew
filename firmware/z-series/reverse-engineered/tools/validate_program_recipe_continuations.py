#!/usr/bin/env python3
"""Record defined OEM continuations separately from retained bounded rejections."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import program_recipe_cases as inputs
import program_recipe_native as native
import run_msp_emulation as common
import run_program_recipe_32bit as historical_width

HISTORICAL = ROOT / "tests/fixtures/program-recipe-original.json"
FIXTURE = ROOT / "tests/fixtures/program-recipe-continuations-original.json"
REPORT = ROOT / "analysis/program-recipe-continuations-validation.json"
INDICES = [351, 568, 570, 571, 572, 576, 578, 579, 580, 584, 586, 587, 588]
TRACE_FIELDS = {"assertion_lines", "assertion_counter", "assertion_delays"}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def application_snapshot(result):
    return {key: copy.deepcopy(value) for key, value in result.items() if key not in TRACE_FIELDS}


def cases():
    all_cases = inputs.cases()
    return [all_cases[index] for index in INDICES]


def source_files():
    return historical_width.source_files() + [ROOT / name for name in (
        "tests/ghidra/EmulateProgramRecipeContinuations.java",
        "tests/run_program_recipe_continuations.py",
        "tests/test_program_recipe_continuations.py",
        "tests/run_msp_emulation.py",
        "tools/validate_program_recipe_continuations.py")]


def verify_original(fixture):
    if fixture["historical_indices"] != INDICES or len(fixture["cases"]) != len(INDICES):
        raise AssertionError("Incomplete returning-assertion subset")
    if fixture["case_sha256"] != hashlib.sha256(json.dumps(cases(), sort_keys=True).encode()).hexdigest():
        raise AssertionError("Original continuation inputs changed")
    raw = common.INPUT.read_bytes()
    if hashlib.sha256(raw).hexdigest() != fixture["input_sha256"]:
        raise AssertionError("Wrong preserved OEM image")
    image = common.images.parse_image(raw)
    segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                for s in image["segments"]]
    if 0x400db014 not in {f["entry"] for f in fixture["original_functions"]}:
        raise AssertionError("Original assertion helper did not execute")
    for function in fixture["original_functions"]:
        for span in function["ranges"]:
            if hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest() != span["sha256"]:
                raise AssertionError("Original instruction bytes changed")


def compare_bounded_policy(fixture, actual):
    historical = json.loads(HISTORICAL.read_text())
    if len(actual) != len(INDICES):
        raise AssertionError("Incomplete native subset")
    differences = []
    for index, original, result in zip(INDICES, fixture["cases"], actual):
        partial = historical["cases"][index]
        if result != partial:
            raise AssertionError("Existing bounded policy changed at case %d" % index)
        if original["assertion_lines"] != [partial["assertion"]] or original["assertion_counter"] != 1 or original["assertion_delays"] != [200]:
            raise AssertionError("Incomplete original assertion side effects at case %d" % index)
        complete = application_snapshot(original)
        changed = [key for key in complete if complete[key] != result[key]]
        if not changed:
            raise AssertionError("A documented deviation was incorrectly treated as equivalence")
        differences.append(dict(historical_index=index, differing_application_fields=changed,
            source_result=result,
            policy="InvalidVolume before statistics" if index == 351 else
                   "NameTruncated before recipe generation" if partial["assertion"] == 626 else
                   "EmptyString before further recipe generation"))
    return differences


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original-directory", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    path = args.original_directory / FIXTURE.name
    fixture = json.loads(path.read_text())
    provenance = json.loads((args.original_directory / REPORT.name).read_text())
    if digest(path) != provenance["fixture_sha256"] or digest(HISTORICAL) != provenance["historical_fixture_sha256"]:
        raise AssertionError("Original or historical fixture changed")
    for name, identity in provenance["generator_files"].items():
        if digest(ROOT / name) != identity:
            raise AssertionError("Original generator changed: " + name)
    verify_original(fixture)
    with tempfile.TemporaryDirectory(prefix="z-recipe-continuations-") as directory:
        actual = native.run(native.build(directory), cases())
    differences = compare_bounded_policy(fixture, actual)
    if args.write_evidence:
        FIXTURE.write_bytes(path.read_bytes())
        report = dict(schema=1, cases=len(INDICES), original_function_bodies=len(fixture["original_functions"]),
            historical_indices=INDICES, fixture_sha256=digest(FIXTURE), historical_fixture_sha256=digest(HISTORICAL),
            input_sha256=fixture["input_sha256"], original_execution_provenance=provenance,
            source_sha256={str(p.relative_to(ROOT)): digest(p) for p in source_files()},
            original_complete_continuations=13, exact_final_state_matches=0,
            intentional_bounded_policy_deviations=differences,
            native_existing_bounded_policy_verified=True,
            native_result_sha256=hashlib.sha256(json.dumps(actual, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
            scope="13 cases previously observed only until the first modeled assertion. Fresh execution runs the OEM assertion body, counter increment and return, then all remaining recipe instructions. Original completed outputs and retained bounded source outputs are explicitly different; no complete-equivalence claim. Scalar math, ROM strings/memory, diagnostic output/delay and local completion notification remain explicit boundaries.",
            hardware_tested=False, full_cpu_emulation=False, complete_application_linked=False)
        REPORT.write_text(json.dumps(report, indent=2) + "\n")
    print("13 complete original continuations; 13 verified intentional bounded-policy deviations")


if __name__ == "__main__":
    main()
