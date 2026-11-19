#!/usr/bin/env python3
"""Build the preserved S/Pro OEM reconstruction or recovery candidate natively.

The archived Python/C/C++ files are never rewritten. A separate execution layer
gives their Path objects consistent ordering and separators, supplies the active
portable compiler/SDK, and verifies the result against the packaged image and
source identities. This builds candidates only; it never opens a device.
"""
import argparse
import builtins
import hashlib
import importlib.util
import json
import os
import pathlib
from pathlib import Path, PurePath, PurePosixPath
import platform
import runpy
import subprocess
import sys
from types import ModuleType


ROOT = Path(__file__).resolve().parents[1]
ADAPTER = Path(__file__).resolve()
ACTIVE = ROOT / "firmware/pico-s-pro/modified"
PROFILES = {
    "oem": {
        "root": ROOT / "firmware/pico-s-pro/reverse-engineered",
        "entry": "build_firmware.py",
        "scripts": ("build_firmware.py", "build.py", "build_sdk_validation.py"),
        "package": "firmware", "report": "firmware-report.json",
        "binary": "pico-pro-recovered.bin", "source_key": "app_sources_sha256",
    },
    "recovery": {
        "root": ACTIVE / "recovery", "entry": "build_recovery.py",
        "scripts": ("build_recovery.py", "build_sdk_validation.py", "embed_recovery_web.py"),
        "package": "firmware/recovery", "report": "recovery-report.json",
        "binary": "pico-web-recovery.bin", "source_key": "project_sources_sha256",
    },
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def path_order(path):
    """Match PosixPath component ordering, including uppercase-before-lowercase."""
    return PurePosixPath(path.as_posix()).parts


def check_windows_path(value):
    # This old vendor compiler is not an extended-path/UNC-aware application.
    # Do not turn \\?\ into //?/ or silently reinterpret a network location.
    if str(value).startswith(("\\\\", "//")):
        raise ValueError(
            "The pinned archive compiler requires local Windows drive paths; "
            "UNC, device and extended-length namespace paths are unsupported: " + str(value)
        )


class BuildPath(type(Path())):
    """Native filesystem operations with the archived Linux path conventions."""

    def __str__(self):
        value = super().__str__()
        if os.name == "nt":
            check_windows_path(value)
            return value.replace("\\", "/")
        return value

    def __lt__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return path_order(self) < path_order(other)

    def __le__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return path_order(self) <= path_order(other)

    def __gt__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return path_order(self) > path_order(other)

    def __ge__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return path_order(self) >= path_order(other)

    def read_text(self, encoding=None, errors=None):
        return super().read_text(encoding=encoding or "utf-8", errors=errors)

    def write_text(self, data, encoding=None, errors=None, newline=None):
        return super().write_text(data, encoding=encoding or "utf-8", errors=errors,
                                  newline="\n" if newline is None else newline)


def rewrite_builder_command(command, profile, component):
    """Adapt only the known archived Python child builders, never compiler calls."""
    if (not isinstance(command, (list, tuple)) or len(command) < 2
            or Path(command[0]).resolve() != Path(sys.executable).resolve()):
        return command
    child = Path(command[1]).resolve()
    allowed = {component / "tools" / name for name in PROFILES[profile]["scripts"]}
    if child not in allowed:
        return command
    return [sys.executable, "-X", "utf8", str(ADAPTER), "--run-script", profile,
            child.name, *command[2:]]


def run_archived_script(profile, name, arguments):
    component = PROFILES[profile]["root"]
    if name not in PROFILES[profile]["scripts"]:
        raise ValueError("Only the preserved build scripts can use this execution adapter")
    script = component / "tools" / name
    # Provide module views only to the archived __main__ module. Do not mutate
    # pathlib, subprocess, builtins or third-party imports process-wide.
    paths = ModuleType("pathlib")
    paths.__dict__.update(vars(pathlib))
    paths.Path = BuildPath
    processes = ModuleType("subprocess")
    processes.__dict__.update(vars(subprocess))

    def run(command, *args, **kwargs):
        return subprocess.run(rewrite_builder_command(command, profile, component), *args, **kwargs)

    processes.run = run

    def import_module(name, globals=None, locals=None, fromlist=(), level=0):
        if level == 0 and name == "pathlib":
            return paths
        if level == 0 and name == "subprocess":
            return processes
        return builtins.__import__(name, globals, locals, fromlist, level)

    original_argv, original_path, bytecode = sys.argv, list(sys.path), sys.dont_write_bytecode
    try:
        sys.argv = [str(script), *arguments]
        sys.path.insert(0, str(script.parent))
        sys.dont_write_bytecode = True
        runpy.run_path(str(script), run_name="__main__", init_globals={
            "__builtins__": dict(vars(builtins), __import__=import_module),
        })
    finally:
        sys.argv = original_argv
        sys.path[:] = original_path
        sys.dont_write_bytecode = bytecode


def archived_inputs(profile):
    component = PROFILES[profile]["root"]
    if profile == "oem":
        manifest = ROOT / "firmware/pico-s-pro/source-manifest.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        inputs = {ROOT / item["destination_path"]: item["sha256"] for item in data["files"]}
    else:
        manifest = component / "import-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        inputs = {component / item["path"]: item["sha256"] for item in data["source_files"]}
    for path in inputs:
        if not path.resolve().is_relative_to(component.resolve()):
            raise ValueError("Archived source manifest points outside its component: " + str(path))
    return manifest, data["source_commit"], inputs


def verify_inputs(inputs):
    for path, expected in inputs.items():
        if path.is_symlink() or not path.is_file() or digest(path) != expected:
            raise ValueError("Preserved archive input changed: " + str(path))


def source_identities(mapping):
    normalized = {}
    for key, value in mapping.items():
        key = key.replace("\\", "/")
        path = PurePosixPath(key)
        if (path.is_absolute() or ".." in path.parts or any(":" in part for part in path.parts)
                or path.as_posix() != key
                or key in normalized):
            raise ValueError("Invalid or duplicated source identity: " + key)
        normalized[key] = value
    return normalized


def verify_result(profile, output):
    spec = PROFILES[profile]
    package = spec["root"] / spec["package"]
    original = json.loads((package / spec["report"]).read_text(encoding="utf-8"))
    rebuilt = json.loads((output / spec["report"]).read_text(encoding="utf-8"))
    expected = original["binary_sha256"]
    actual = digest(output / spec["binary"])
    if digest(package / spec["binary"]) != expected or actual != expected:
        raise ValueError("Rebuilt archive binary differs from its preserved package")
    if (rebuilt["binary_sha256"] != actual or not rebuilt["sdk_link_passed"]
            or not rebuilt["image_layout"]["passed"]
            or rebuilt["image_layout"] != original["image_layout"]):
        raise ValueError("Rebuilt archive failed its native SDK or image layout checks")
    source_key = spec["source_key"]
    if source_identities(rebuilt[source_key]) != source_identities(original[source_key]):
        raise ValueError("Rebuilt archive source identities differ from its preserved package")

    def compiled_sources(report):
        return [(item["source"].replace("\\", "/"), item["sha256"])
                for item in report["compiled_sources"]]

    if compiled_sources(rebuilt) != compiled_sources(original):
        raise ValueError("Rebuilt archive compiled sources or their order differ from its preserved package")
    if profile == "oem":
        update_hash = digest(output / "pico-pro-recovered.update.txt")
        if update_hash != original["update_body_sha256"] or rebuilt["update_body_sha256"] != update_hash:
            raise ValueError("Rebuilt OEM update envelope differs from its preserved package")
    return {"binary_sha256": actual, "binary_bytes": (output / spec["binary"]).stat().st_size,
            "binary_matches_package": True, "image_layout_matches_package": True,
            "project_source_identities_match": True,
            "compiled_source_identities_and_order_match": True}


def find_arm_prefix():
    module_path = ACTIVE / "tools/arm_toolchain.py"
    spec = importlib.util.spec_from_file_location("archive_arm_toolchain", module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.find_arm_prefix(ACTIVE)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "--run-script":
        if len(argv) < 3 or argv[1] not in PROFILES:
            raise ValueError("Invalid archived builder invocation")
        run_archived_script(argv[1], argv[2], argv[3:])
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=PROFILES)
    parser.add_argument("--machine", choices=("pico-s-pro",), default="pico-s-pro")
    parser.add_argument("--sdk-root", type=Path,
                        default=Path(os.environ.get("PICOBREW_SDK", str(ACTIVE / ".deps/energia"))))
    parser.add_argument("--arm-prefix", default=os.environ.get("PICOBREW_ARM_PREFIX"))
    parser.add_argument("--output-dir", type=Path,
                        help="Generated files; defaults to build/archives/<profile> at repository root")
    args = parser.parse_args(argv)
    profile, spec = args.profile, PROFILES[args.profile]
    sdk = args.sdk_root.resolve()
    prefix = args.arm_prefix or find_arm_prefix()
    if not prefix or not (sdk / "cores/cc3200/Energia.h").is_file():
        parser.error("Compiler/SDK missing; run make firmware-bootstrap or the active "
                     "firmware tools/bootstrap_dependencies.py, or supply --arm-prefix and --sdk-root")
    output = (args.output_dir or ROOT / "build/archives" / profile).resolve()
    if output.is_relative_to(spec["root"]) and not output.is_relative_to(spec["root"] / "build"):
        parser.error("Generated output must not overwrite the preserved source tree")
    if os.name == "nt":
        for path in (ROOT, sdk, output, prefix):
            check_windows_path(path)
        prefix = str(prefix).replace("\\", "/")
    manifest, commit, inputs = archived_inputs(profile)
    verify_inputs(inputs)
    output.mkdir(parents=True, exist_ok=True)
    # The original OEM builder also writes application archives to its ignored
    # component-local build/ directory. No preserved source file is an output.
    command = [sys.executable, "-X", "utf8", str(ADAPTER), "--run-script", profile,
               spec["entry"], "--sdk-root", str(sdk), "--arm-prefix", str(prefix),
               "--output-dir", str(output)]
    provenance = {
        "schema_version": 1, "profile": profile, "machine": args.machine,
        "adapter": {"path": ADAPTER.relative_to(ROOT).as_posix(), "sha256": digest(ADAPTER)},
        "runtime_compatibility": {
            "scope": "Only the preserved Python build entry points and their known child builders",
            "source_rewriting": False,
            "path_ordering": "Case-sensitive POSIX path components, matching the archived Linux builds",
            "path_rendering": "Forward slashes for native compiler arguments and report identities",
            "text_io": "UTF-8 reports with LF newlines",
        },
        "archive_manifest": manifest.relative_to(ROOT).as_posix(),
        "archive_manifest_sha256": digest(manifest), "archive_commit": commit,
        "preserved_input_files": len(inputs), "preserved_inputs_unchanged": False,
        "archived_builder_sha256": {name: digest(spec["root"] / "tools" / name)
                                    for name in spec["scripts"]},
        "host": {"system": platform.system(), "machine": platform.machine(),
                 "python": platform.python_version()},
        "compiler_prefix": str(prefix), "sdk_root": str(sdk),
        "compiler": subprocess.check_output([str(prefix) + "gcc", "--version"],
                                            text=True).splitlines()[0],
        "output_directory": str(output), "hardware_executed": False,
        "status": "building", "validation": None,
    }
    try:
        subprocess.run(command, check=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1",
                                                    PYTHONUTF8="1"))
        provenance["validation"] = verify_result(profile, output)
        provenance["status"] = "passed"
    except BaseException as exc:
        provenance["status"] = "failed"
        provenance["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        try:
            verify_inputs(inputs)
            provenance["preserved_inputs_unchanged"] = True
        except BaseException as exc:
            provenance["status"] = "failed"
            provenance["error"] = f"{type(exc).__name__}: {exc}"
            raise
        finally:
            (output / "portable-build.json").write_text(json.dumps(provenance, indent=2) + "\n",
                                                        encoding="utf-8", newline="\n")
    print(json.dumps({"profile": profile, **provenance["validation"],
                      "preserved_input_files": len(inputs), "preserved_inputs_unchanged": True,
                      "adapter_report": str(output / "portable-build.json")}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print("Archive build failed: " + str(error), file=sys.stderr)
        raise SystemExit(error.returncode if isinstance(error, subprocess.CalledProcessError) else 1)
