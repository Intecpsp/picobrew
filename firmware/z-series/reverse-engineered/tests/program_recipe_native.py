"""Build/run the real selected JSON adapter and reconstructed program writer."""
import json
import copy
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["program_diagnostics", "enums", "program_recipe", "program_repository", "program_catalog", "program_writer",
           "program_runtime", "program_runtime_execution", "runtime_instruction", "quantity"]


def dependency():
    pin = json.loads((ROOT / "dependencies/arduinojson.json").read_text())
    result = Path(os.environ.get("ARDUINOJSON_DIR", ROOT / "build/dependencies" / pin["directory"]))
    if not (result / "src/ArduinoJson.h").is_file():
        subprocess.run([sys.executable, str(ROOT / "tools/acquire_arduinojson.py")], check=True)
    sys.path.insert(0, str(ROOT / "tools"))
    try:
        import acquire_arduinojson
        acquire_arduinojson.verify_source(result)
    finally:
        sys.path.remove(str(ROOT / "tools"))
    return result


def build(directory, extra_flags=()):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    common = list(extra_flags) + ["-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(ROOT / "include")]
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + ["-std=c11"] + common +
                   ["-c", str(ROOT / "src/msp_protocol.c"), "-o", str(directory / "msp.o")],
                   check=True, capture_output=True, text=True)
    command = shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++11", "-fno-fast-math", "-ffp-contract=off"] + common
    command += ["-I", str(dependency() / "src"), str(ROOT / "tests/program_recipe_probe.cpp"),
                str(ROOT / "adapters/arduinojson/program_recipe_json.cpp")]
    command += [str(ROOT / ("src/" + name + ".cpp")) for name in SOURCES]
    command += [str(directory / "msp.o"), "-o", str(directory / "probe")]
    subprocess.run(command, check=True, capture_output=True, text=True)
    return directory / "probe"


def run(executable, cases, emulator=()):
    lines = []
    for value in cases:
        lines.append("%d %d %d %.17g %.17g %d %d %d %d %s" % (value["action"], value["capacity"],
            value["nesting"], value["pressure"], value["maximum"], value["abv"], value["ibu"],
            value["resumed"], value["session"], value["json"].encode().hex()))
    output = subprocess.run(list(emulator) + [str(executable)], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True)
    return [json.loads(line) for line in output.stdout.splitlines()]


def expected(case, original, long_bytes):
    result = copy.deepcopy(original)
    changes = json.loads((ROOT / "analysis/program-recipe-compatibility.json").read_text())
    if case["action"] == 0:
        for difference in changes["differences"] + (changes["host_only"] if long_bytes == 8 else []):
            if case["json"] == difference["json"]:
                result.update(difference["selected_override"])
    return result
