#!/usr/bin/env python3
"""Compile the portable standalone engine with a pinned ESP32/ESP8266 compiler."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("api.cpp", "engine.cpp", "history.cpp", "history_records.cpp", "records.cpp", "telemetry.cpp", "values.cpp", "http_server.c")
HEADERS = ("include/picobrew/device_services.hpp", "include/picobrew/standalone_engine.hpp", "include/picobrew/http_server.h",
           "src/engine_internal.hpp", "src/json.hpp", "src/records.hpp")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--architecture", choices=("esp8266", "esp32"), required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    model = "picoferm" if args.architecture == "esp8266" else "picostill"
    pin_path = ROOT.parent / model / "reverse-engineered/dependencies/toolchain.json"
    for path, expected in json.loads(pin_path.read_text())["identity_sha256"].items():
        if digest(args.toolchain / path) != expected:
            raise ValueError("Compiler identity mismatch: " + path)
    prefix = "xtensa-lx106-elf" if args.architecture == "esp8266" else "xtensa-esp32-elf"
    output = args.output or ROOT / "build" / args.architecture
    output.mkdir(parents=True, exist_ok=True)
    flags = ["-Os", "-Wall", "-Wextra", "-Werror", "-mlongcalls", "-ffunction-sections", "-fdata-sections",
             "-fno-fast-math", "-ffp-contract=off", "-U__STRICT_ANSI__"]
    if args.architecture == "esp8266":
        flags += ["-mtext-section-literals", "-falign-functions=4"]
    report = {"schema_version": 1, "architecture": args.architecture, "flags": flags, "sources": [],
              "headers": {path: digest(ROOT / path) for path in HEADERS}, "build_script_sha256": digest(__file__),
              "dependency_pin_sha256": digest(pin_path), "complete_application_linked": False, "hardware_tested": False,
              "scope": "The portable engine and unchanged S/Pro HTTP transport compile with the pinned target compiler. SDK/platform drivers, assets and complete application linkage are recorded by each machine profile."}
    objects = []
    for name in SOURCES:
        cpp = name.endswith(".cpp")
        compiler = args.toolchain / "bin" / (prefix + ("-g++" if cpp else "-gcc"))
        language = ["-std=gnu++11", "-fno-exceptions", "-fno-rtti"] if cpp else ["-std=gnu99"]
        source = ROOT / "src" / name
        destination = output / (name + ".o")
        subprocess.run([str(compiler), *language, *flags, "-I" + str(ROOT / "include"), "-c", str(source), "-o", str(destination)], check=True)
        objects.append(str(destination))
        report["sources"].append({"source": "src/" + name, "source_sha256": digest(source), "object_sha256": digest(destination), "object_bytes": destination.stat().st_size})
    compiler = args.toolchain / "bin" / (prefix + "-g++")
    report["compiler"] = subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0]
    report["compiler_sha256"] = digest(compiler)
    archive = output / "libpicobrew_standalone.a"
    if archive.exists():
        archive.unlink()
    subprocess.run([str(args.toolchain / "bin" / (prefix + "-ar")), "rcs", str(archive), *objects], check=True)
    report["archive_sha256"] = digest(archive)
    (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Compiled {len(SOURCES)} common modules for {args.architecture}; object evidence only")


if __name__ == "__main__":
    main()
