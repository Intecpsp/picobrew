#!/usr/bin/env python3
"""Compile recovered modules and real ESP32 adapters; deliberately no flash link."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import acquire_arduinojson
import acquire_esp32

ROOT = Path(__file__).resolve().parents[1]
ESP32_PIN = json.loads(acquire_esp32.PIN.read_text())
ESP32_PACKAGES = {package["name"]: package for package in ESP32_PIN["packages"]}
SDK_IDENTITIES = ESP32_PACKAGES["arduino-esp32"]["identity_sha256"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--arduino", type=Path, required=True)
    parser.add_argument("--arduinojson", type=Path, required=True,
                        help="Hash-pinned ArduinoJson checkout from tools/acquire_arduinojson.py")
    parser.add_argument("--output", type=Path, default=ROOT / "build/esp32")
    args = parser.parse_args()
    compiler = args.toolchain / "bin/xtensa-esp32-elf-g++"
    c_compiler = args.toolchain / "bin/xtensa-esp32-elf-gcc"
    archive = args.toolchain / "bin/xtensa-esp32-elf-ar"
    arduino = args.arduino.resolve()
    sdk = arduino / "tools/sdk"
    if not compiler.is_file() or not (sdk / "include/config/sdkconfig.h").is_file():
        parser.error("Use the documented GCC 5.2 toolchain and Arduino ESP32 1.0.1 checkout")
    acquire_esp32.verify_identity(args.toolchain, ESP32_PACKAGES["xtensa-esp32-elf"])
    for path, expected in SDK_IDENTITIES.items():
        if digest(arduino / path) != expected:
            raise SystemExit("Pinned Arduino SDK identity mismatch: " + path)
    arduinojson = args.arduinojson.resolve()
    acquire_arduinojson.verify_source(arduinojson)
    args.output.mkdir(parents=True, exist_ok=True)
    includes = [ROOT / "include", ROOT / "platform/esp32", arduino / "cores/esp32", arduino / "variants/esp32"]
    includes += [arduino / "libraries" / library / "src" for library in ("Wire", "SPI", "WiFi")]
    includes.append(arduino / "libraries/EEPROM")
    includes.append(arduinojson / "src")
    includes += sorted(path for path in (sdk / "include").iterdir() if path.is_dir())
    flags = ["-Os", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-ffunction-sections", "-fdata-sections",
             "-fstrict-volatile-bitfields", "-mlongcalls", "-fno-fast-math", "-ffp-contract=off", "-DESP_PLATFORM",
             "-DESP32", "-DARDUINO_ARCH_ESP32", "-DARDUINO=10805", "-DF_CPU=240000000L", "-DCORE_DEBUG_LEVEL=0", "-DHAVE_CONFIG_H"]
    cpp_flags = ["-std=gnu++11", "-fno-exceptions", "-fno-rtti"]
    sources = sorted((ROOT / "src").glob("*.cpp")) + sorted((ROOT / "src").glob("*.c"))
    sources += sorted((ROOT / "platform/esp32").glob("*.cpp"))
    sources += sorted((ROOT / "adapters/arduinojson").glob("*.cpp"))
    objects, evidence = [], []
    for source in sources:
        output = args.output / (source.relative_to(ROOT).as_posix().replace("/", "_") + ".o")
        program = compiler if source.suffix == ".cpp" else c_compiler
        language_flags = cpp_flags if source.suffix == ".cpp" else ["-std=gnu99"]
        subprocess.run([str(program)] + language_flags + flags + ["-I" + str(path) for path in includes] +
                       ["-c", str(source), "-o", str(output)], check=True)
        objects.append(output)
        evidence.append({"source": source.relative_to(ROOT).as_posix(), "source_sha256": digest(source),
                         "object_sha256": digest(output), "object_bytes": output.stat().st_size})
    library = args.output / "libzseries_recovered_esp32.a"
    subprocess.run([str(archive), "rcs", str(library)] + [str(path) for path in objects], check=True)
    report = {"schema_version": 1, "compiler": subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0],
              "arduino_reference": "espressif/arduino-esp32@9a7946e6859130146e56231c27a4aa177d9175a1 (1.0.1)",
              "sdk_config_sha256": digest(sdk / "include/config/sdkconfig.h"), "compiler_sha256": digest(compiler),
              "common_flags": flags, "cpp_flags": cpp_flags, "sources": evidence,
              "archive_sha256": digest(library), "complete_application_linked": False, "hardware_tested": False,
              "scope": "Target object compilation of owned source against the evidenced historical SDK; no application entry point, partition selection, firmware packaging, or installation"}
    report["sdk_identity_sha256"] = SDK_IDENTITIES
    report["esp32_dependency_pin_sha256"] = digest(acquire_esp32.PIN)
    report["arduinojson_dependency"] = json.loads((ROOT / "dependencies/arduinojson.json").read_text())
    report["build_script_sha256"] = digest(__file__)
    report["owned_header_sha256"] = {path.relative_to(ROOT).as_posix(): digest(path) for path in
                                     sorted((ROOT / "include/zseries").glob("*")) if path.is_file()}
    report["platform_header_sha256"] = {path.relative_to(ROOT).as_posix(): digest(path) for path in
                                        sorted((ROOT / "platform/esp32").glob("*.hpp"))}
    (args.output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Compiled {len(sources)} recovered modules/adapters and archived {library.name}; no flash image produced")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
