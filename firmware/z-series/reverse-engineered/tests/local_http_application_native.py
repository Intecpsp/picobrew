"""Real local HTTP application with pinned ArduinoJson, buffer and mailbox."""
import json
import os
from pathlib import Path
import shlex
import subprocess
from program_recipe_native import dependency, ROOT, SOURCES

APPLICATION_SOURCES = SOURCES + ["program_list", "local_http_application", "local_http_server", "local_http_state", "local_http", "http_assets"]


def build(directory, source="local_http_application_probe.cpp", extra_flags=()):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    common = ["-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(ROOT / "include")] + list(extra_flags)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + ["-std=c11"] + common +
                   ["-c", str(ROOT / "src/msp_protocol.c"), "-o", str(directory / "msp.o")], check=True)
    binary = directory / Path(source).stem
    command = shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++11", "-fno-fast-math", "-ffp-contract=off"] + common
    command += ["-isystem", str(dependency() / "src"), str(ROOT / "tests" / source),
                str(ROOT / "adapters/arduinojson/program_recipe_json.cpp")]
    command += [str(ROOT / ("src/" + name + ".cpp")) for name in APPLICATION_SOURCES]
    command += [str(directory / "msp.o"), "-o", str(binary)]
    subprocess.run(command, check=True)
    return binary


def run(binary, cases):
    rows = ["%u %.17g %.17g %s %s %s %s" % (value["pending"], value["pressure"], value["maximum"],
            value["path"].encode().hex() or "-", value["json"].encode().hex() or "-", value["current_hex"], value["queued_hex"])
            for value in cases]
    completed = subprocess.run([str(binary)], input="\n".join(rows) + "\n", capture_output=True, text=True, check=True)
    return [json.loads(line) for line in completed.stdout.splitlines()]
