#!/usr/bin/env python3
"""Reproduce original Z119 text input fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

import run_msp_emulation as common
import head_text_cases as inputs

ROOT = common.COMPONENT
SCRIPT = ROOT / "tests/ghidra/EmulateHeadText.java"
HELPER = ROOT / "tools/ghidra/Z119Execution.java"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--ghidra-home", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    if args.output.exists() or not 1 <= args.timeout <= 1800:
        parser.error("Use a new output path and timeout in 1..1800 seconds")
    raw = common.INPUT.read_bytes()
    parsed = common.images.parse_image(raw)
    if common.shared.digest(common.INPUT) != common.images.IMAGES[common.INPUT.name]:
        raise ValueError("Wrong original image")
    segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"]) for s in parsed["segments"]]
    ghidra, java = args.ghidra_home.resolve(), args.java_home.resolve()
    evidence = common.shared.tool_evidence(ghidra, java, {
        "ghidra": ghidra.parent / common.shared.TOOL_PINS["ghidra"]["filename"],
        "jdk": java.parent / common.shared.TOOL_PINS["jdk"]["filename"],
    }, "Xtensa:LE:32:default")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="head-text-original-", dir=output.parent) as directory:
        directory = Path(directory)
        scripts = directory / "scripts"
        scripts.mkdir()
        for source in (SCRIPT, HELPER):
            shutil.copy2(source, scripts / source.name)
        case_file, layout = directory / "cases.json", directory / "layout.json"
        cases = inputs.cases()
        case_file.write_text(json.dumps(cases, sort_keys=True) + "\n")
        layout.write_text(json.dumps(dict(input_path=str(common.INPUT), segments=segments)) + "\n")
        fixture = directory / "head-text-original.json"
        command = [str(ghidra / "support/analyzeHeadless"), str(args.project.resolve()), "firmware", "-process", common.INPUT.name,
                   "-readOnly", "-noanalysis", "-max-cpu", "1", "-scriptPath", str(scripts), "-postScript", SCRIPT.name,
                   str(case_file), str(layout), str(fixture)]
        env = dict(os.environ, JAVA_HOME=str(java))
        env["PATH"] = str(java / "bin") + os.pathsep + env.get("PATH", "")
        log = output.with_name(output.name + ".headless.log")
        result = common.shared.run_bounded(command, env, log, args.timeout)
        if result or not fixture.exists():
            raise RuntimeError(f"Original Head text input execution incomplete: {log}")
        original = common.shared.read_json(fixture)
        if original["input_sha256"] != common.images.IMAGES[common.INPUT.name] or len(original["cases"]) != len(cases):
            raise RuntimeError("Original Head text input fixture identity or count differs")
        for function in original["original_functions"]:
            for span in function["ranges"]:
                if hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest() != span["sha256"]:
                    raise RuntimeError("Executed original function bytes differ")
        output.mkdir()
        shutil.copy2(fixture, output / fixture.name)
        report = dict(schema_version=1, input_sha256=common.images.IMAGES[common.INPUT.name], cases=len(cases),
                      fixture_sha256=common.shared.digest(fixture), tools=evidence,
                      generator_files={str(p.relative_to(ROOT)): common.shared.digest(p) for p in (SCRIPT, HELPER, Path(__file__), Path(inputs.__file__))},
                      execution=dict(project_read_only=True, analysis=False, max_cpu=1, process_timeout_seconds=args.timeout),
                      hardware_tested=False, full_cpu_emulation=False, complete_esp32_firmware_rebuild=False)
        (output / "head-text-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Wrote {len(cases)} original Head text input lookups to {output}")


if __name__ == "__main__":
    main()
