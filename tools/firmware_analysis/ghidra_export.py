#!/usr/bin/env python3
"""Run pinned, bounded Ghidra analysis on an explicitly mapped firmware image.

The output is address-traceable decompiler evidence, not recovered buildable
source or proof that all functions were discovered. No firmware is executed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
SCRIPTS = ROOT / "ghidra_scripts"
TOOL_PINS = {
    "ghidra": {"filename": "ghidra_12.2_PUBLIC_20261016.zip", "bytes": 569712708,
               "sha256": "2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67"},
    "jdk": {"filename": "OpenJDK25U-jdk_x64_linux_hotspot_25.0.5_8.tar.gz", "bytes": 141341188,
            "sha256": "68982529869257d0ee6b824ec5aa270ff8b118bb438670da0b97ebdab178d7fb"},
}
LANGUAGES = {"ARM:LE:32:Cortex": "ARM", "Xtensa:LE:32:default": "Xtensa"}
MAX_IMAGE = 16 * 1024 * 1024


def digest(path):
    sha = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON field: {key}")
        result[key] = value
    return result


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def integer(value, field):
    if isinstance(value, bool):
        raise ValueError(f"{field} must be an integer")
    try:
        number = int(value, 0) if isinstance(value, str) else value
    except (ValueError, TypeError) as error:
        raise ValueError(f"{field} must be an integer") from error
    if not isinstance(number, int) or number < 0 or number > 0xFFFFFFFF:
        raise ValueError(f"{field} must be an unsigned 32-bit integer")
    return number


def normalized_layout(layout, data):
    """Validate explicit segments; never guess a device memory map or ABI."""
    if not isinstance(layout, dict) or layout.get("schema_version") != 1:
        raise ValueError("Expected layout schema_version 1")
    if not 16 <= len(data) <= MAX_IMAGE:
        raise ValueError("Image must contain 16 bytes to 16 MiB")
    expected = layout.get("input_sha256")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("Layout must pin input_sha256")
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("Input image does not match the layout SHA-256")
    language = layout.get("language_id")
    if language not in LANGUAGES or layout.get("compiler_spec", "default") != "default":
        raise ValueError("Use the verified Cortex-M or Xtensa language and default compiler spec")
    thumb = layout.get("thumb", language == "ARM:LE:32:Cortex")
    if not isinstance(thumb, bool) or thumb != (language == "ARM:LE:32:Cortex"):
        raise ValueError("Thumb context must match the selected processor")
    source_segments = layout.get("segments")
    if not isinstance(source_segments, list) or not 1 <= len(source_segments) <= 128:
        raise ValueError("Expected 1 to 128 explicit segments")
    segments, names = [], set()
    for raw in source_segments:
        if not isinstance(raw, dict):
            raise ValueError("Each segment must be an object")
        name = raw.get("name")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,63}", name) or name in names:
            raise ValueError("Segment names must be unique simple identifiers")
        names.add(name)
        segment = {"name": name, **{field: integer(raw.get(field), field) for field in ("offset", "address", "size")}}
        for field in ("read", "write", "execute"):
            if not isinstance(raw.get(field), bool):
                raise ValueError(f"Segment {field} permission must be explicit")
            segment[field] = raw[field]
        if segment["offset"] + segment["size"] > len(data) or segment["address"] + segment["size"] > 0x100000000:
            raise ValueError("Segment exceeds its input or address space")
        if segment["size"] and not segment["read"]:
            raise ValueError("Imported initialized segments must be readable")
        if thumb and segment["execute"] and segment["address"] & 1:
            raise ValueError("Thumb executable regions must begin on a halfword")
        segments.append(segment)
    populated = sorted((x for x in segments if x["size"]), key=lambda x: x["address"])
    if not populated:
        raise ValueError("At least one segment must contain bytes")
    for first, second in zip(populated, populated[1:]):
        if first["address"] + first["size"] > second["address"]:
            raise ValueError("Overlapping memory regions need a reviewed overlay design")

    def mapped(address, executable=False):
        return any(s["address"] <= address < s["address"] + s["size"] and
                   (not executable or s["execute"]) for s in populated)

    entries = layout.get("entry_points")
    if not isinstance(entries, list) or not 1 <= len(entries) <= 1024:
        raise ValueError("Expected 1 to 1024 explicit entry points")
    entries = sorted(set(integer(value, "entry point") for value in entries))
    if any(not mapped(value, True) or (thumb and value & 1) for value in entries):
        raise ValueError("Entry points must be untagged addresses in executable segments")
    source_labels = layout.get("labels", [])
    if not isinstance(source_labels, list) or len(source_labels) > 50000:
        raise ValueError("Too many or malformed labels")
    labels, label_keys = [], set()
    for raw in source_labels:
        if not isinstance(raw, dict):
            raise ValueError("Each label must be an object")
        name = raw.get("name")
        address = integer(raw.get("address"), "label address")
        function = raw.get("function", True)
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,159}", name):
            raise ValueError("Labels must use simple source identifiers")
        if not isinstance(function, bool) or not mapped(address, function) or (thumb and function and address & 1):
            raise ValueError("Label is outside its declared memory type")
        if (address, name) in label_keys:
            raise ValueError("Duplicate label")
        label_keys.add((address, name))
        label = {"address": address, "name": name, "function": function}
        for field in ("evidence", "confidence"):
            if field in raw:
                if not isinstance(raw[field], str) or len(raw[field]) > 4096:
                    raise ValueError("Label evidence must be bounded text")
                label[field] = raw[field]
        labels.append(label)
    return {"schema_version": 1, "input_sha256": expected, "language_id": language,
            "compiler_spec": "default", "thumb": thumb, "segments": segments,
            "entry_points": entries, "labels": sorted(labels, key=lambda x: (x["address"], x["name"]))}


def tool_evidence(ghidra, java, archives, language):
    result = {"archives": {}, "installed_files": {}}
    for kind, archive in archives.items():
        pin = TOOL_PINS[kind]
        if archive.stat().st_size != pin["bytes"] or digest(archive) != pin["sha256"]:
            raise ValueError(f"{kind} archive does not match the pinned distribution")
        result["archives"][kind] = pin
    properties = ghidra / "Ghidra/application.properties"
    if "application.version=12.2" not in properties.read_text():
        raise ValueError("Installed Ghidra version does not match 12.2")
    paths = [properties, ghidra / "support/analyzeHeadless", ghidra / "support/launch.sh",
             ghidra / "Ghidra/Features/Decompiler/os/linux_x86_64/decompile"]
    for subdir in ("Ghidra/Framework", "Ghidra/Features/Base/lib", "Ghidra/Features/Decompiler/lib"):
        paths.extend(sorted((ghidra / subdir).rglob("*.jar")))
    processor = ghidra / "Ghidra/Processors" / LANGUAGES[language]
    paths.extend(sorted((processor / "data/languages").glob("*")))
    paths.extend(sorted((processor / "lib").glob("*.jar")))
    for path in paths:
        if path.is_file():
            result["installed_files"]["ghidra/" + path.relative_to(ghidra).as_posix()] = digest(path)
    for name in ("bin/java", "release", "lib/modules"):
        path = java / name
        result["installed_files"]["jdk/" + name] = digest(path)
    return result


def run_bounded(command, env, log, timeout):
    with log.open("wb") as stream:
        process = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise RuntimeError(f"Ghidra exceeded the {timeout}-second process deadline; inspect {log}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ghidra-home", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--ghidra-archive", type=Path)
    parser.add_argument("--java-archive", type=Path)
    parser.add_argument("--project-output", type=Path,
                        help="Keep the analyzed Ghidra project here (ignored analysis cache, not deterministic evidence)")
    parser.add_argument("--analysis-timeout", type=int, default=600)
    parser.add_argument("--function-timeout", type=int, default=20)
    parser.add_argument("--process-timeout", type=int, default=3600)
    parser.add_argument("--max-functions", type=int, default=20000)
    options = parser.parse_args(argv)
    for value, maximum, name in ((options.analysis_timeout, 3600, "analysis timeout"),
                                  (options.function_timeout, 300, "function timeout"),
                                  (options.process_timeout, 10800, "process timeout"),
                                  (options.max_functions, 50000, "maximum functions")):
        if not 1 <= value <= maximum:
            parser.error(f"Invalid {name}")
    if os.name != "posix" or not sys.platform.startswith("linux"):
        parser.error("The pinned analysis distributions require Linux x86-64")
    source, output = options.input.resolve(), options.output.resolve()
    if output.exists():
        parser.error("Output already exists; select a new path so previous evidence is preserved")
    if options.project_output and options.project_output.exists():
        parser.error("Project output already exists; select a new cache path")
    if source.stat().st_size > MAX_IMAGE:
        parser.error("Input exceeds 16 MiB")
    layout = normalized_layout(read_json(options.layout), source.read_bytes())
    ghidra, java = options.ghidra_home.resolve(), options.java_home.resolve()
    archives = {"ghidra": options.ghidra_archive or ghidra.parent / TOOL_PINS["ghidra"]["filename"],
                "jdk": options.java_archive or java.parent / TOOL_PINS["jdk"]["filename"]}
    evidence = tool_evidence(ghidra, java, archives, layout["language_id"])
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ghidra-export-", dir=output.parent) as temp:
        temporary = Path(temp)
        stage = temporary / "export"
        stage.mkdir()
        project = options.project_output.resolve() if options.project_output else temporary / "project"
        project.mkdir(parents=True)
        scripts = temporary / "scripts"
        shutil.copytree(SCRIPTS, scripts)
        pipeline_hashes = {"ghidra_export.py": digest(Path(__file__)),
            **{"ghidra_scripts/" + p.name: digest(p) for p in sorted(scripts.glob("*.java"))}}
        config = temporary / "layout.json"
        config.write_text(json.dumps({**layout, "input_path": str(source)}), encoding="utf-8")
        log = output.parent / (output.name + ".headless.log")
        command = [str(ghidra / "support/analyzeHeadless"), str(project), "firmware",
                   "-import", str(source), "-processor", layout["language_id"],
                   "-cspec", layout["compiler_spec"], "-loader", "BinaryLoader",
                   "-scriptPath", str(scripts), "-preScript", "ConfigureFirmware.java", str(config),
                   "-postScript", "ExportFirmware.java", str(stage), str(options.function_timeout),
                   str(options.max_functions), "-analysisTimeoutPerFile", str(options.analysis_timeout),
                   "-max-cpu", "1"]
        if not options.project_output:
            command.append("-deleteProject")
        env = os.environ.copy()
        env["JAVA_HOME"] = str(java)
        env["PATH"] = str(java / "bin") + os.pathsep + env.get("PATH", "")
        env["LC_ALL"] = "C.UTF-8"
        result = run_bounded(command, env, log, options.process_timeout)
        summary_path = stage / "ghidra-summary.json"
        if result != 0 or not summary_path.is_file():
            raise RuntimeError(f"Ghidra failed or did not complete its export; inspect {log}")
        summary = read_json(summary_path)
        if summary["language_id"] != layout["language_id"]:
            raise RuntimeError("Exported processor differs from the requested layout")
        # Record actual installed analysis files after any first-use SLA compilation.
        evidence = tool_evidence(ghidra, java, archives, layout["language_id"])
        files = {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}
        manifest = {"schema_version": 1, "input": {"filename": source.name, "bytes": source.stat().st_size,
                    "sha256": digest(source)}, "layout": layout, "tools": evidence,
                    "pipeline_sha256": pipeline_hashes, "layout_file_sha256": digest(options.layout),
                    "limits": {"analysis_seconds": options.analysis_timeout, "function_seconds": options.function_timeout,
                               "process_seconds": options.process_timeout, "max_functions": options.max_functions, "analysis_cpus": 1},
                    "files": files, "buildable_source": False, "behaviorally_verified": False,
                    "hardware_tested": False, "analysis_timed_out": summary["analysis_timed_out"]}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        stage.rename(output)
    print(json.dumps({"output": str(output), "input_sha256": manifest["input"]["sha256"],
                      "functions": summary["functions_recognized"], "decompiler_failures": summary["decompilation_failed"],
                      "unresolved_names": summary["unresolved_names"], "analysis_timed_out": summary["analysis_timed_out"]}))
    return 2 if summary["analysis_timed_out"] else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"firmware analysis: {error}", file=sys.stderr)
        raise SystemExit(1)
