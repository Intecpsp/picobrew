"""Portable archive adapter regressions, without compiling or modifying archives."""
import hashlib
import json
from pathlib import Path, PureWindowsPath
import pathlib
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import build_archive as adapter


class PathCompatibilityTests(unittest.TestCase):
    def test_windows_paths_use_the_original_case_sensitive_component_order(self):
        paths = [PureWindowsPath("C:/library/b.cpp"), PureWindowsPath("C:/library/a/B.cpp"),
                 PureWindowsPath("C:/library/A.cpp"), PureWindowsPath("C:/library/a.cpp")]
        self.assertEqual([path.as_posix() for path in sorted(paths, key=adapter.path_order)], [
            "C:/library/A.cpp", "C:/library/a/B.cpp", "C:/library/a.cpp", "C:/library/b.cpp",
        ])
        actual = [adapter.BuildPath("library/b.cpp"), adapter.BuildPath("library/A.cpp")]
        self.assertEqual([path.name for path in sorted(actual)], ["A.cpp", "b.cpp"])

    def test_unc_and_extended_windows_paths_are_rejected_explicitly(self):
        for path in (r"\\server\share\compiler", r"\\?\C:\compiler", r"\\.\C:\compiler",
                     "//server/share/compiler", "//?/C:/compiler"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "local Windows drive"):
                adapter.check_windows_path(path)
        adapter.check_windows_path("C:/Folder with spaces/compiler")

    def test_only_known_archived_python_child_scripts_are_wrapped(self):
        component = adapter.PROFILES["oem"]["root"]
        command = [sys.executable, str(component / "tools/build.py"), "--sdk-root", "SDK with spaces"]
        adapted = adapter.rewrite_builder_command(command, "oem", component)
        self.assertEqual(adapted[:6], [sys.executable, "-X", "utf8", str(adapter.ADAPTER),
                                      "--run-script", "oem"])
        self.assertEqual(adapted[6:], ["build.py", "--sdk-root", "SDK with spaces"])
        compiler = ["C:/Toolchain with spaces/arm-none-eabi-gcc", "source.c"]
        self.assertIs(adapter.rewrite_builder_command(compiler, "oem", component), compiler)
        unrelated = [sys.executable, str(component / "tests/test_sensors.py")]
        self.assertIs(adapter.rewrite_builder_command(unrelated, "oem", component), unrelated)

    @unittest.skipIf(os.name == "nt", "Windows canonical TEMP aliases are covered by the runpy fixture")
    def test_child_routing_canonicalizes_the_component_path_too(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            component = root / "component"
            (component / "tools").mkdir(parents=True)
            alias = root / "alias"
            alias.symlink_to(component, target_is_directory=True)
            command = [sys.executable, str(component / "tools/build.py"), "--archive-only"]
            adapted = adapter.rewrite_builder_command(command, "oem", alias)
            self.assertEqual(adapted[4:], ["--run-script", "oem", "build.py", "--archive-only"])

    def test_only_cpp_compilation_prioritizes_the_high_level_spi_header(self):
        includes = ["-I SDK with spaces/cores/cc3200", "-I SDK with spaces/system/driverlib",
                    "-I SDK with spaces/libraries/WiFi", "-I SDK with spaces/libraries/SPI"]
        cpp = ["C:/GNU Arm/bin/arm-none-eabi-g++.exe", *includes, "-c", "entry.cpp", "-o", "entry.o"]
        self.assertEqual(adapter.prioritize_cpp_spi_header(cpp), [cpp[0], includes[0], includes[3],
                         includes[1], includes[2], *cpp[5:]])
        c = ["arm-none-eabi-gcc", *includes, "-c", "spi_hal.c", "-o", "spi_hal.o"]
        self.assertIs(adapter.prioritize_cpp_spi_header(c), c)
        link = ["arm-none-eabi-g++", *includes, "entry.o", "-o", "app.elf"]
        self.assertIs(adapter.prioritize_cpp_spi_header(link), link)

    def test_script_execution_is_confined_and_preserves_exact_source_bytes(self):
        original_path = pathlib.Path
        original_run = subprocess.run
        with tempfile.TemporaryDirectory(prefix="archive adapter with spaces ") as folder:
            component = Path(folder)
            (component / "tools").mkdir()
            output = component / "result.json"
            script = component / "tools/build_firmware.py"
            code = (
                "from pathlib import Path\nimport json\nimport subprocess\nimport sys\n"
                "root = Path(__file__).resolve().parents[1]\n"
                "subprocess.run([sys.executable, str(root / 'tools/build.py'), '--archive-only'], check=True)\n"
                "paths = sorted([Path('library/b.cpp'), Path('library/A.cpp')])\n"
                "Path(sys.argv[1]).write_text(json.dumps([str(p) for p in paths]) + '\\n')\n"
            ).encode("utf-8")
            script.write_bytes(code)
            spec = dict(adapter.PROFILES["oem"], root=component)
            with patch.dict(adapter.PROFILES, oem=spec), patch.object(subprocess, "run") as run:
                adapter.run_archived_script("oem", "build_firmware.py", [str(output)])
                self.assertEqual(run.call_args.args[0][-2:], ["build.py", "--archive-only"])
                self.assertEqual(run.call_args.args[0][4:6], ["--run-script", "oem"])
            self.assertEqual(script.read_bytes(), code)
            self.assertEqual(output.read_bytes(), b'["library/A.cpp", "library/b.cpp"]\n')
            self.assertFalse((component / "tools/__pycache__").exists())
        self.assertIs(pathlib.Path, original_path)
        self.assertIs(subprocess.run, original_run)


class IdentityTests(unittest.TestCase):
    def test_report_keys_normalize_without_dropping_duplicate_identities(self):
        self.assertEqual(adapter.source_identities({r"tools\build.py": "hash"}),
                         {"tools/build.py": "hash"})
        for invalid in ({r"tools\build.py": "a", "tools/build.py": "b"},
                        {"../outside.c": "hash"}, {"C:/outside.c": "hash"}):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                adapter.source_identities(invalid)

    def test_changed_archive_bytes_fail_verification(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "input.c"
            path.write_bytes(b"original")
            expected = {path: hashlib.sha256(b"original").hexdigest()}
            adapter.verify_inputs(expected)
            path.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "Preserved archive input changed"):
                adapter.verify_inputs(expected)

    def test_binary_source_hashes_and_compilation_order_are_all_checked(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            package, output = root / "package", root / "output"
            package.mkdir()
            output.mkdir()
            payload = b"synthetic firmware image"
            report = {
                "binary_sha256": hashlib.sha256(payload).hexdigest(),
                "sdk_link_passed": True, "image_layout": {"passed": True, "binary_bytes": len(payload)},
                "project_sources_sha256": {"src/program.c": "original", "tools/build.py": "frozen"},
                "compiled_sources": [{"source": "A.cpp", "sha256": "a"},
                                     {"source": "b.cpp", "sha256": "b"}],
            }
            spec = dict(adapter.PROFILES["recovery"], root=root, package="package")
            for directory in (package, output):
                (directory / spec["binary"]).write_bytes(payload)
                (directory / spec["report"]).write_text(json.dumps(report), encoding="utf-8")
            with patch.dict(adapter.PROFILES, recovery=spec):
                self.assertTrue(adapter.verify_result("recovery", output)["binary_matches_package"])
                modified = dict(report, compiled_sources=list(reversed(report["compiled_sources"])))
                (output / spec["report"]).write_text(json.dumps(modified), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "compiled sources or their order"):
                    adapter.verify_result("recovery", output)
                modified = dict(report, project_sources_sha256={"src/program.c": "different"})
                (output / spec["report"]).write_text(json.dumps(modified), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "source identities"):
                    adapter.verify_result("recovery", output)
                (output / spec["report"]).write_text(json.dumps(report), encoding="utf-8")
                (output / spec["binary"]).write_bytes(b"different image")
                with self.assertRaisesRegex(ValueError, "binary differs"):
                    adapter.verify_result("recovery", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
