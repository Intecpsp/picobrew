#!/usr/bin/env python3
"""Compare POST dispatch, actual recipe output and complete Core-event payloads."""
import argparse
import copy
import gzip
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import local_http_application_cases as inputs
import local_http_application_native as native
import run_msp_emulation as original_common

FIXTURE = ROOT / "tests/fixtures/local-http-application-original.json.gz"
REPORT = ROOT / "analysis/local-http-application-validation.json"
TARGET = ROOT / "analysis/local-http-application-target-build.json"
SOURCE_PATHS = ["include/zseries/local_http_application.hpp", "src/local_http_application.cpp",
               "platform/esp32/local_http_platform.hpp", "platform/esp32/local_http_platform.cpp",
               "tests/ghidra/EmulateLocalHttpApplication.java", "tests/local_http_application_cases.py",
               "tests/local_http_application_native.py", "tests/local_http_application_probe.cpp",
               "tests/local_http_application_integration.cpp", "tests/run_local_http_application_emulation.py", "tests/test_local_http_application.py",
               "tests/core_event_fixture.hpp", "tools/validate_core_event.py",
               "tools/validate_local_http_application.py"]
SOURCE_PATHS += ["src/" + name + ".cpp" for name in native.APPLICATION_SOURCES if name != "local_http_application"]
SOURCE_PATHS += ["adapters/arduinojson/program_recipe_json.cpp", "adapters/arduinojson/recipe_document.hpp",
                "dependencies/arduinojson.json", "tests/run_msp_emulation.py", "tests/program_recipe_cases.py",
                "tests/program_recipe_native.py", "src/msp_protocol.c"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def application_snapshot(result):
    """Assertion side effects are retained separately from application fields."""
    return {key: copy.deepcopy(value) for key, value in result.items()
            if key not in ("assertion_lines", "assertion_counter", "assertion_delays")}


def expected(case, original):
    """Validate explicit bounded rejection, never relabel it OEM equivalence."""
    result = application_snapshot(original)
    if case["path"] == "/recipe" and case["json"] in ("null", "[]", "{}"):
        if original["assertion_lines"] != [339, 626] or original["assertion_counter"] != 2 or original["assertion_delays"] != [200, 200]:
            raise AssertionError("Missing returning original assertion side effects")
        if result["assertion"] != 339 or result["status"] != 200 or result["body"] != "":
            raise AssertionError("Missing defined OEM continuation to HTTP success")
        if result["buffer"] != dict(type=15, id=0, name="", code=[[57, 256], [40, 0]], strings=[""]):
            raise AssertionError("Unexpected original unnamed LocalBrew program")
        if not result["pending"]:
            raise AssertionError("Original did not publish or retain its pending event")
        result.update(status=400, length=21, mime="text/plain", body="program build error 3")
        result["buffer"] = dict(type=15, id=0, name="<EMPTY>", code=[], strings=[])
        result["parameters"] = dict(id=0, abv=0xffffffff, ibu=0xffffffff,
            **{name: ["0000000000000000", 8] for name in ("time", "water", "intensity", "temperature")}, resumed=0)
        result["stats"] = dict(rate="0000000000000000", duration=0, pressure="0000000000000000", maximum="0000000000000000", added=0)
        result["current_hex"] = case["current_hex"][8:]
        result["queued_hex"] = case["queued_hex"][8:]
        result["pending"] = case["pending"]
    elif original["assertion_lines"] or original["assertion_counter"] or original["assertion_delays"]:
        raise AssertionError("Unreviewed diagnostic assertion in an equivalence case")
    return result


def verify_original_ranges(fixture):
    raw = original_common.INPUT.read_bytes()
    if hashlib.sha256(raw).hexdigest() != fixture["input_sha256"]:
        raise AssertionError("Wrong preserved original image")
    image = original_common.images.parse_image(raw)
    segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                for s in image["segments"]]
    for function in fixture["original_functions"]:
        for span in function["ranges"]:
            value = original_common.read_mapped(raw, segments, span["address"], span["size"])
            if hashlib.sha256(value).hexdigest() != span["sha256"]:
                raise AssertionError("Original executed bytes do not match preserved image")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original-directory", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true")
    parser.add_argument("--target-report", type=Path)
    args = parser.parse_args()
    cases = inputs.cases()
    path = args.original_directory / "local-http-application-original.json"
    original = json.loads(path.read_text())
    provenance = json.loads((args.original_directory / "local-http-application-validation.json").read_text())
    if original["mode"] != "full" or len(original["cases"]) != len(cases):
        raise AssertionError("Incomplete original application comparison")
    if provenance["case_sha256"] != hashlib.sha256(json.dumps(cases, sort_keys=True).encode()).hexdigest():
        raise AssertionError("Original execution inputs differ from current cases")
    for name, identity in provenance["generator_files"].items():
        if digest(ROOT / name) != identity:
            raise AssertionError("Original generator changed: " + name)
    verify_original_ranges(original)
    with tempfile.TemporaryDirectory(prefix="z-http-application-") as directory:
        binary = native.build(directory)
        actual = native.run(binary, cases)
    exact = 0
    for case, oem, actual_case in zip(cases, original["cases"], actual):
        if actual_case != expected(case, oem):
            fields = [key for key in actual_case if actual_case[key] != expected(case, oem)[key]]
            raise AssertionError("Application mismatch: %s %s" % (case["id"], fields))
        exact += actual_case == application_snapshot(oem)
    if args.write_evidence:
        fixture = dict(input_sha256=original["input_sha256"], original_functions=original["original_functions"],
                       cases=[dict(input=case, original=value) for case, value in zip(cases, original["cases"])])
        FIXTURE.write_bytes(gzip.compress((json.dumps(fixture, separators=(",", ":")) + "\n").encode(), mtime=0))
        report = dict(schema_version=1, input_sha256=original["input_sha256"], cases=len(cases),
                      exact_matches=exact, intentional_bounded_rejections=len(cases)-exact,
                      all_compatibility_expectations_pass=True, hardware_tested=False,
                      full_cpu_emulation=False, complete_application_linked=False,
                      original_functions=original["original_functions"], modeled_boundaries=original["boundary_calls"],
                      scope=original["scope"], source_sha256={p: digest(ROOT / p) for p in SOURCE_PATHS},
                      golden_fixture_sha256=digest(FIXTURE), original_fixture_sha256=digest(path),
                      compiler=subprocess.check_output(shlex.split(os.environ.get("CXX", "c++")) + ["--version"], text=True).splitlines()[0],
                      original_execution_provenance=provenance,
                      bounded_repair="Six null/array/empty-object recipes execute returning OEM diagnostic assertions at lines 339 and 626, increment the global assertion counter twice, delay 200 ms twice, construct an unnamed LocalBrew program, publish ProgramLoaded, and return HTTP 200. The existing bounded builder intentionally stops at EmptyString: code/string counts and statistics remain empty/default, prior Core event/mailbox data are retained, and HTTP returns 400 without publishing. These are defined-continuation input-validation deviations, not state equivalence or undefined behavior.")
        REPORT.write_text(json.dumps(report, indent=2) + "\n")
        if args.target_report:
            target = json.loads(args.target_report.read_text())
            paths = ["src/local_http_application.cpp", "platform/esp32/local_http_platform.cpp"]
            target["sources"] = [s for s in target["sources"] if s["source"] in paths]
            if len(target["sources"]) != len(paths):
                raise AssertionError("Target report lacks concrete application or socket adapter")
            for item in target["sources"]:
                if digest(ROOT / item["source"]) != item["source_sha256"]:
                    raise AssertionError("Target compilation is stale")
            target["owned_header_sha256"] = {k:v for k,v in target["owned_header_sha256"].items() if
                k in ("include/zseries/local_http_application.hpp", "include/zseries/local_http_server.hpp",
                      "include/zseries/local_http_state.hpp", "include/zseries/program_list.hpp", "include/zseries/program_recipe_json.hpp")}
            target["platform_header_sha256"] = {k:v for k,v in target["platform_header_sha256"].items() if k=="platform/esp32/local_http_platform.hpp"}
            target["scope"] = "Concrete incoming HTTP application and WiFiServer/WiFiClient adapter compiled against pinned historical SDK; full application lifecycle and hardware behavior remain unqualified."
            TARGET.write_text(json.dumps(target, indent=2) + "\n")
    print("%u original application cases: %u exact matches, %u intentional bounded rejections" % (len(cases), exact, len(cases)-exact))


if __name__ == "__main__":
    main()
