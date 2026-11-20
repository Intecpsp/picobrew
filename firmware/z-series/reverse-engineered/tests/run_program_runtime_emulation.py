#!/usr/bin/env python3
"""Execute original Z119 runtime functions in a preserved read-only Ghidra project."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

import program_runtime_cases as inputs
import run_msp_emulation as common

SCRIPT = common.COMPONENT / "tests/ghidra/EmulateProgramRuntime.java"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--ghidra-home", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--smoke", action="store_true")
    options = parser.parse_args()
    if not 1 <= options.timeout <= 1800 or options.output.exists():
        parser.error("Use a new output path and a timeout in 1..1800 seconds")
    raw = common.INPUT.read_bytes()
    parsed = common.images.parse_image(raw)
    input_sha = hashlib.sha256(raw).hexdigest()
    if input_sha != common.images.IMAGES[common.INPUT.name]:
        parser.error("Preserved image differs from its recorded hash")
    segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                for s in parsed["segments"]]
    ghidra, java = options.ghidra_home.resolve(), options.java_home.resolve()
    evidence = common.shared.tool_evidence(ghidra, java, {
        "ghidra": ghidra.parent / common.shared.TOOL_PINS["ghidra"]["filename"],
        "jdk": java.parent / common.shared.TOOL_PINS["jdk"]["filename"],
    }, "Xtensa:LE:32:default")
    cases = inputs.cases()
    if options.smoke:
        cases = [case for i, case in enumerate(cases) if case["group"] == "dispatch" or i % 31 == 0]
    specification = dict(words=inputs.WORDS, doubles=inputs.DOUBLES, strings=inputs.STRINGS, cases=cases)
    output = options.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    log = output.with_name(output.name + ".headless.log")
    with tempfile.TemporaryDirectory(prefix="runtime-original-", dir=output.parent) as temporary:
        temporary = Path(temporary)
        scripts = temporary / "scripts"
        scripts.mkdir()
        shutil.copy2(SCRIPT, scripts / SCRIPT.name)
        spec = temporary / "cases.json"
        spec.write_text(json.dumps(specification, sort_keys=True), encoding="utf-8")
        layout = temporary / "layout.json"
        layout.write_text(json.dumps(dict(input_path=str(common.INPUT), segments=segments)), encoding="utf-8")
        fixture = temporary / "program-runtime-original.json"
        command = [str(ghidra / "support/analyzeHeadless"), str(options.project.resolve()), "firmware",
                   "-process", common.INPUT.name, "-readOnly", "-noanalysis", "-max-cpu", "1",
                   "-scriptPath", str(scripts), "-postScript", SCRIPT.name, str(spec), str(layout), str(fixture)]
        env = dict(os.environ, JAVA_HOME=str(java))
        env["PATH"] = str(java / "bin") + os.pathsep + env.get("PATH", "")
        status = common.shared.run_bounded(command, env, log, options.timeout)
        if status or not fixture.is_file():
            raise RuntimeError(f"Original runtime execution did not complete: {log}")
        result = common.shared.read_json(fixture)
        if result["input_sha256"] != input_sha or len(result["cases"]) != len(cases):
            raise RuntimeError("Incomplete or wrong-image instruction fixtures")
        for function in result["original_functions"]:
            for span in function["ranges"]:
                digest = hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest()
                if digest != span["sha256"]:
                    raise RuntimeError("Executed function range differs from original bytes")
        result["mode"] = "smoke" if options.smoke else "full"
        fixture.write_text(json.dumps(result, separators=(",", ":")) + "\n", encoding="utf-8")
        provenance = dict(schema=1, input_sha256=input_sha,
            fixture_sha256=common.shared.digest(fixture), cases=len(cases),
            generator_files={str(p.relative_to(common.COMPONENT)): common.shared.digest(p)
                for p in [SCRIPT, Path(__file__), Path(inputs.__file__), Path(common.__file__)]},
            tools=evidence, execution=dict(analysis=False, project_read_only=True, max_cpu=1,
                process_timeout_seconds=options.timeout, instruction_budget_per_case=200000),
            scope=result["scope"], hardware_tested=False, full_cpu_emulation=False,
            complete_esp32_firmware_rebuild=False)
        output.mkdir()
        shutil.copyfile(fixture, output / fixture.name)
        (output / "program-runtime-validation.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(cases)} original runtime cases to {output}")


if __name__ == "__main__":
    main()
