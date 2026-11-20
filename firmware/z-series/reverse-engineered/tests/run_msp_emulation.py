#!/usr/bin/env python3
"""Regenerate MSP original-instruction fixtures from an existing verified Z119 project.

This uses shared pinned-tool verification and process bounds. It never opens an
I2C device, flashes a controller, or modifies the analyzed Ghidra project.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile

COMPONENT = Path(__file__).resolve().parents[1]
REPO = COMPONENT.parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


shared = module("shared_firmware_analysis", REPO / "tools/firmware_analysis/ghidra_export.py")
images = module("zseries_image_analysis", COMPONENT / "tools/analyze_images.py")
INPUT = COMPONENT.parent / "oem/upstream/zseries_0_0_119.bin"
SCRIPT = COMPONENT / "tests/ghidra/EmulateMsp.java"


def read_mapped(raw, segments, address, size):
    for segment in segments:
        if segment["address"] <= address and address + size <= segment["address"] + segment["size"]:
            offset = segment["offset"] + address - segment["address"]
            return raw[offset:offset + size]
    raise ValueError("Original function range is outside mapped image")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--ghidra-home", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory for fixtures and provenance")
    parser.add_argument("--timeout", type=int, default=600)
    options = parser.parse_args()
    if not 1 <= options.timeout <= 1800:
        parser.error("timeout must be in 1..1800 seconds")
    if options.output.exists():
        parser.error("Select a new output directory; previous evidence is preserved")
    raw = INPUT.read_bytes()
    parsed = images.parse_image(raw)
    input_sha = hashlib.sha256(raw).hexdigest()
    if input_sha != images.IMAGES[INPUT.name]:
        parser.error("Preserved OEM image does not match its recorded hash")
    segments = [{"address": segment["load_address"], "offset": segment["file_offset"],
                 "size": segment["size_bytes"]} for segment in parsed["segments"]]
    ghidra, java = options.ghidra_home.resolve(), options.java_home.resolve()
    tools = shared.tool_evidence(ghidra, java, {
        "ghidra": ghidra.parent / shared.TOOL_PINS["ghidra"]["filename"],
        "jdk": java.parent / shared.TOOL_PINS["jdk"]["filename"],
    }, "Xtensa:LE:32:default")
    output = options.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    log = output.with_name(output.name + ".headless.log")
    with tempfile.TemporaryDirectory(prefix="msp-original-", dir=output.parent) as temp:
        temporary = Path(temp)
        scripts = temporary / "scripts"
        scripts.mkdir()
        shutil.copy2(SCRIPT, scripts / SCRIPT.name)
        mapping = temporary / "input.json"
        mapping.write_text(json.dumps({"input_path": str(INPUT), "segments": segments}), encoding="utf-8")
        fixture = temporary / "msp-original.json"
        command = [str(ghidra / "support/analyzeHeadless"), str(options.project.resolve()), "firmware",
                   "-process", INPUT.name, "-readOnly", "-noanalysis", "-max-cpu", "1",
                   "-scriptPath", str(scripts), "-postScript", SCRIPT.name, str(fixture), "full", str(mapping)]
        env = dict(os.environ, JAVA_HOME=str(java))
        env["PATH"] = str(java / "bin") + os.pathsep + env.get("PATH", "")
        status = shared.run_bounded(command, env, log, options.timeout)
        # Headless Ghidra can return zero after a script exception: the completed
        # artifact and all original-byte ranges are checked independently.
        if status != 0 or not fixture.is_file():
            raise RuntimeError(f"Original-instruction fixture generation failed; inspect {log}")
        result = shared.read_json(fixture)
        if result["mode"] != "full" or result["input_sha256"] != input_sha or len(result["setup"]) != 11:
            raise RuntimeError("Incomplete original-instruction fixture output")
        for function in result["original_functions"]:
            for span in function["ranges"]:
                actual = hashlib.sha256(read_mapped(raw, segments, span["address"], span["size"])).hexdigest()
                if actual != span["sha256"]:
                    raise RuntimeError("Executed function bytes differ from original OEM image")
        provenance = {
            "schema": 1,
            "input_sha256": input_sha,
            "fixture_sha256": shared.digest(fixture),
            "generator_files": {
                "tests/ghidra/EmulateMsp.java": shared.digest(scripts / SCRIPT.name),
                "tests/run_msp_emulation.py": shared.digest(Path(__file__)),
            },
            "tools": tools,
            "execution": {"analysis": False, "project_read_only": True, "max_cpu": 1,
                          "process_timeout_seconds": options.timeout,
                          "instruction_budget_per_case": 6000000},
            "scope": result["scope"],
            "hardware_tested": False,
            "full_cpu_emulation": False,
            "complete_esp32_firmware_rebuild": False,
        }
        output.mkdir()
        shutil.copyfile(fixture, output / fixture.name)
        (output / "msp-protocol-validation.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote validated MSP fixtures and exact provenance to {output}")


if __name__ == "__main__":
    main()
