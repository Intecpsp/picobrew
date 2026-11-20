#!/usr/bin/env python3
"""Replay historical recipe observations, including 13 first-assertion snapshots."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

import acquire_recipe_i386 as toolchain
import program_recipe_cases as inputs
import program_recipe_native as native


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_files():
    root = native.ROOT
    files = [root / ("src/" + name + ".cpp") for name in native.SOURCES]
    files += [root / "src/msp_protocol.c"]
    files += [root / ("include/zseries/" + name) for name in (
        "program_recipe.hpp", "program_recipe_json.hpp", "program_repository.hpp",
        "program_runtime.hpp", "runtime_instruction.hpp", "quantity.hpp", "enums.hpp", "msp_protocol.h")]
    files += [root / name for name in (
        "adapters/arduinojson/program_recipe_json.cpp", "adapters/arduinojson/recipe_document.hpp",
        "dependencies/arduinojson.json", "tools/acquire_arduinojson.py",
        "tests/program_recipe_probe.cpp", "tests/program_recipe_native.py",
        "tests/program_recipe_cases.py", "tests/acquire_recipe_i386.py",
        "tests/run_program_recipe_32bit.py", "tests/toolchains/recipe-i386.json",
        "analysis/program-recipe-compatibility.json")]
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, default=native.ROOT / "tests/fixtures/program-recipe-original.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new output file")
    root = toolchain.acquire(args.toolchain)
    pin = json.loads(toolchain.PIN.read_text())
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    major = subprocess.check_output(compiler + ["-dumpversion"], text=True).strip().split(".")[0]
    if major != str(pin["compiler_major"]):
        parser.error("The optional multilib fixture uses GCC 14 headers/libraries; select a GCC 14 CXX/CC")
    sysroot = root / "sysroot"
    flags = ["-m32", "-static", "-msse2", "-mfpmath=sse",
        "-isystem", str(sysroot / "usr/include/x86_64-linux-gnu/c++/14/32"),
        "-isystem", str(sysroot / "usr/include"),
        "-isystem", str(sysroot / "usr/include/x86_64-linux-gnu"),
        "-isystem", "/usr/include/x86_64-linux-gnu",
        "-B" + str(sysroot / "usr/lib/gcc/x86_64-linux-gnu/14/32") + "/",
        "-B" + str(sysroot / "usr/lib32") + "/", "-L" + str(sysroot / "usr/lib32")]
    original = json.loads(args.fixture.read_text())
    cases = inputs.cases()
    if original["mode"] != "full" or len(original["cases"]) != len(cases):
        parser.error("A complete current original fixture is required")
    emulator = [str(root / "usr/bin/qemu-i386")]
    with tempfile.TemporaryDirectory(prefix="z119-json-i386-") as temporary:
        executable = native.build(temporary, flags)
        abi = json.loads(subprocess.check_output(emulator + [str(executable), "--abi"], text=True))
        if abi != dict(pointer=4, long=4, json_integer=4, local_capacity=1024, http_capacity=4096):
            raise RuntimeError("Rebuild does not have the required 32-bit JSON layout")
        actual = native.run(executable, cases, emulator)
        changed = []
        for index, (case, expected, result) in enumerate(zip(cases, original["cases"], actual)):
            if result != native.expected(case, expected, 4):
                raise AssertionError(f"Unexplained 32-bit original mismatch at case {index}: {case}")
            if result != expected:
                changed.append(index)
        if len(actual) != len(cases):
            raise RuntimeError("Incomplete 32-bit replay")
        subprocess.run(emulator + [str(executable), "--integration"], check=True)
        executable_sha = digest(executable)
    driver = Path(shutil.which(compiler[0])).resolve()
    frontend = Path(subprocess.check_output(compiler + ["-print-prog-name=cc1plus"], text=True).strip()).resolve()
    report = dict(schema=1, fixture_sha256=digest(args.fixture),
        input_sha256=original["input_sha256"], cases=len(cases), abi=abi,
        application_cases=sum(c["action"] != 0 for c in cases),
        all_application_cases_match_historical_observations=True,
        complete_application_matches=497, first_assertion_observations=13,
        application_comparison_scope="497 complete application outputs and 13 snapshots at the historical executor's first modeled assertion. The latter do not represent completed OEM behavior; fresh returning-assertion execution in program-recipe-continuations-validation.json documents the retained bounded-policy deviations.",
        documented_compatibility_cases=changed,
        result_sha256=hashlib.sha256(json.dumps(actual, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
        executable_sha256=executable_sha, runtime_integration_passed=True,
        source_sha256={str(p.relative_to(native.ROOT)): digest(p) for p in source_files()},
        compiler=dict(version=subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0],
            driver_name=driver.name, driver_sha256=digest(driver), frontend_sha256=digest(frontend)),
        toolchain_pin_sha256=digest(toolchain.PIN), qemu_binary_sha256=digest(emulator[0]),
        flags=[value.replace(str(root), "${I386_TOOLCHAIN}") for value in flags],
        target="i386 Linux application acceptance under qemu-i386; not ESP32 hardware execution",
        hardware_tested=False, complete_esp32_firmware_rebuild=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Accepted {len(cases)} historical 32-bit observations (497 complete application matches, 13 first-assertion snapshots); {len(changed)} documented malformed-token differences")


if __name__ == "__main__":
    main()
