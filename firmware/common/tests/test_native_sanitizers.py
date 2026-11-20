"""Exercise the common engine and inherited HTTP transport under ASan/UBSan."""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
REFERENCE = ROOT.parent / "pico-s-pro/modified"


@unittest.skipIf(sys.platform == "win32", "Native Windows runs the portable integration suite without ASan")
class SanitizerTests(unittest.TestCase):
    def test_actual_engine_and_transport_memory_safety(self):
        flags = ["-g", "-O1", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie"]
        include = "-I" + str(ROOT / "include")
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        with tempfile.TemporaryDirectory(prefix="common-engine-sanitize-") as temporary:
            directory = pathlib.Path(temporary)
            transport = directory / "http.o"
            subprocess.run(["gcc", "-std=c11", *flags, include, "-c", str(ROOT / "src/http_server.c"), "-o", str(transport)], check=True)
            for fixture in ("http_server_fixture", "http_asset_fixture"):
                executable = directory / fixture
                subprocess.run(["gcc", "-std=c11", *flags, "-no-pie", include, str(REFERENCE / "tests" / (fixture + ".c")), str(transport), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True, env=env)
            executable = directory / "engine"
            sources = [ROOT / "src" / name for name in ("engine.cpp", "api.cpp", "values.cpp", "records.cpp", "history.cpp", "history_records.cpp", "telemetry.cpp")]
            subprocess.run(["g++", "-std=c++11", *flags, "-no-pie", "-DENGINE_SANITIZER_MAIN", include,
                            *map(str, sources), str(ROOT / "tests/engine_bridge.cpp"), str(transport), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True, env=env)


if __name__ == "__main__":
    unittest.main(verbosity=2)
