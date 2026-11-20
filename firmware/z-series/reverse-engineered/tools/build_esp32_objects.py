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
import acquire_ethernet2
import adapt_ethernet2
import recover_backend_credentials

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[2]
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
    parser.add_argument("--ethernet2", type=Path, default=ROOT / "build/dependencies/Ethernet2-0bab6349fb5b42fce2d5442bbed093279184b548")
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
    credentials = recover_backend_credentials.generate(args.output / "generated")
    ethernet2 = adapt_ethernet2.prepare(args.ethernet2, args.output / "ethernet2-adapted")
    includes = [args.output / "generated", ethernet2 / "src", ROOT / "include", ROOT / "platform/esp32", arduino / "cores/esp32", arduino / "variants/esp32"]
    includes += [arduino / "libraries" / library / "src" for library in ("Wire", "SPI", "WiFi", "WiFiClientSecure", "Update")]
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
    # Compile the selected dependency separately, preserving upstream warning policy.
    dependency_objects = []
    for source in sorted((ethernet2 / "src").rglob("*.cpp")):
        output = args.output / ("ethernet2_" + source.relative_to(ethernet2).as_posix().replace("/", "_") + ".o")
        subprocess.run([str(compiler)] + cpp_flags + flags + ["-w"] + ["-I" + str(path) for path in includes] +
                       ["-c", str(source), "-o", str(output)], check=True)
        dependency_objects.append(output)
    subprocess.run([str(archive), "rcs", str(args.output / "libethernet2_adapted.a")] + [str(path) for path in dependency_objects], check=True)
    library = args.output / "libzseries_recovered_esp32.a"
    if library.exists():
        library.unlink()
    subprocess.run([str(archive), "rcs", str(library)] + [str(path) for path in objects], check=True)
    report = {"schema_version": 1, "compiler": subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0],
              "arduino_reference": "espressif/arduino-esp32@9a7946e6859130146e56231c27a4aa177d9175a1 (1.0.1)",
              "sdk_config_sha256": digest(sdk / "include/config/sdkconfig.h"), "compiler_sha256": digest(compiler),
              "common_flags": flags, "cpp_flags": cpp_flags, "sources": evidence,
              "archive_sha256": digest(library), "complete_application_linked": False, "hardware_tested": False,
              "scope": "Target object compilation of owned source against the evidenced historical SDK. Full application linking and packaging require separate evidence; no device installation is performed."}
    report["sdk_identity_sha256"] = SDK_IDENTITIES
    report["esp32_dependency_pin_sha256"] = digest(acquire_esp32.PIN)
    report["arduinojson_dependency"] = json.loads((ROOT / "dependencies/arduinojson.json").read_text())
    report["ethernet2_dependency"] = json.loads(acquire_ethernet2.PIN.read_text())
    report["ethernet2_adapter_sha256"] = digest(ROOT / "tools/adapt_ethernet2.py")
    report["ethernet2_objects"] = {path.name: digest(path) for path in dependency_objects}
    report["build_script_sha256"] = digest(__file__)
    report["original_backend_constants"] = credentials
    report["owned_header_sha256"] = {path.relative_to(ROOT).as_posix(): digest(path) for path in
                                     sorted((ROOT / "include/zseries").glob("*")) if path.is_file()}
    report["platform_header_sha256"] = {path.relative_to(ROOT).as_posix(): digest(path) for path in
                                        sorted((ROOT / "platform/esp32").glob("*.hpp"))}
    report["adapter_header_sha256"] = {path.relative_to(ROOT).as_posix(): digest(path) for path in
                                      sorted((ROOT / "adapters").rglob("*.hpp"))}
    report["implementation_include_sha256"] = {path.relative_to(ROOT).as_posix(): digest(path) for path in
        sorted((ROOT / "src").glob("*.inc")) + sorted((ROOT / "platform/esp32").glob("*.inc"))}
    # Keep the generators and their checked source inputs with the compiled
    # objects. A later link cannot certify new tooling against an old archive.
    build_inputs = [ROOT / "tools" / name for name in (
        "build_esp32_objects.py", "acquire_esp32.py", "acquire_arduinojson.py",
        "acquire_ethernet2.py", "adapt_ethernet2.py", "recover_backend_credentials.py",
        "analyze_images.py")]
    build_inputs += [acquire_esp32.PIN, acquire_arduinojson.PIN, acquire_ethernet2.PIN,
                     ROOT.parent / "oem/upstream/zseries_0_0_119.bin"]
    report["build_input_sha256"] = {path.relative_to(REPOSITORY).as_posix(): digest(path)
                                   for path in sorted(build_inputs)}
    (args.output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Compiled {len(sources)} recovered modules/adapters and archived {library.name}; no flash image produced")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
