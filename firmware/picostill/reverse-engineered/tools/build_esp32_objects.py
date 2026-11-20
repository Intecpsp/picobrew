#!/usr/bin/env python3
"""Build owned PicoStill modules and real ESP32 drivers; no application link."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = ROOT / "dependencies/toolchain.json"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--arduino", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/esp32")
    args = parser.parse_args()
    pin = json.loads(PIN.read_text())
    for path, expected in pin["identity_sha256"].items():
        if digest(args.toolchain / path) != expected:
            raise ValueError("Pinned compiler identity mismatch: "+path)
    compiler = args.toolchain / "bin/xtensa-esp32-elf-g++"
    arduino=args.arduino.resolve()
    sdk_pin=json.loads((ROOT / "dependencies/arduino-esp32.json").read_text())
    for path,expected in sdk_pin["identity_sha256"].items():
        if digest(arduino/path)!=expected: raise ValueError("Pinned SDK identity mismatch: "+path)
    flags = ["-std=gnu++11", "-Os", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti",
             "-ffunction-sections", "-fdata-sections", "-mlongcalls", "-fno-fast-math", "-ffp-contract=off",
             "-Wno-unused-parameter", "-DESP_PLATFORM", "-DESP32", "-DARDUINO_ARCH_ESP32", "-DARDUINO=10805",
             "-DF_CPU=240000000L", "-DCORE_DEBUG_LEVEL=0", "-DHAVE_CONFIG_H"]
    args.output.mkdir(parents=True, exist_ok=True)
    sources, objects = [], []
    includes=[ROOT/"include", ROOT/"platform/esp32", arduino/"cores/esp32", arduino/"variants/esp32", arduino/"libraries/Wire/src", arduino/"libraries/WiFi/src", arduino/"libraries/Update/src"]
    includes += sorted(p for p in (arduino/"tools/sdk/include").iterdir() if p.is_dir())
    for source in sorted((ROOT / "src").glob("*.cpp"))+sorted((ROOT/"platform/esp32").glob("*.cpp")):
        out = args.output / (source.relative_to(ROOT).as_posix().replace("/","_")+".o")
        subprocess.run([str(compiler)]+flags+["-I"+str(path) for path in includes]+["-c", str(source), "-o", str(out)], check=True)
        sources.append({"source": source.relative_to(ROOT).as_posix(), "source_sha256": digest(source),
                        "object_sha256": digest(out), "object_bytes": out.stat().st_size})
        objects.append(str(out))
    archive = args.output / "libpicostill_recovered_esp32.a"
    if archive.exists(): archive.unlink() # Do not retain members from removed/renamed sources.
    subprocess.run([str(args.toolchain / "bin/xtensa-esp32-elf-ar"), "rcs", str(archive)]+objects, check=True)
    report = {"schema_version": 1, "compiler": subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0],
              "compiler_sha256": digest(compiler), "dependency_pin_sha256": digest(PIN), "flags": flags,
              "sources": sources, "archive_sha256": digest(archive), "build_script_sha256": digest(__file__),
              "headers": {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted((ROOT / "include/picostill").glob("*.hpp"))},
              "platform_headers": {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted((ROOT/"platform/esp32").glob("*.hpp"))},
              "arduino_dependency_sha256": digest(ROOT/"dependencies/arduino-esp32.json"),
              "complete_application_linked": False, "hardware_tested": False,
              "scope": "Xtensa ESP32 object compilation of recovered application modules and concrete Arduino GPIO/PWM/I2C/clock/serial, NVS, Wi-Fi/HTTP clients and flash-update drivers. Includes concrete application startup; full SDK linking is recorded separately. No partition mapping, flash package or physical qualification."}
    (args.output / "build-report.json").write_text(json.dumps(report, indent=2)+"\n")
    print("Compiled %d PicoStill modules with pinned Xtensa GCC; no firmware image produced" % len(sources))


if __name__ == "__main__":
    main()
