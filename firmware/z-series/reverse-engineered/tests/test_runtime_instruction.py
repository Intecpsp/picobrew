"""Original-execution fixtures for owned runtime instruction interpretation."""
import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeInstructionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="zseries-runtime-instruction-")
        cls.addClassCleanup(cls.temporary.cleanup)
        build = Path(cls.temporary.name)
        bridge = build / "bridge.cpp"
        bridge.write_text('''#include "zseries/runtime_instruction.hpp"
using namespace zseries::oem;
extern "C" int valid(unsigned op, unsigned arg) {
    return instruction_valid({static_cast<OpCode>(op), arg});
}
extern "C" int temperature(unsigned op, unsigned arg, double* out) {
    return instruction_temperature({static_cast<OpCode>(op), arg}, *out);
}
extern "C" int common(unsigned value) { return is_common_string_reference(value); }
''')
        library = build / "instructions.so"
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run(compiler + ["-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                       "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared", "-I", str(ROOT / "include"),
                       str(bridge), str(ROOT / "src/runtime_instruction.cpp"), "-o", str(library)],
                       check=True, capture_output=True, text=True)
        cls.native = ctypes.CDLL(str(library))
        cls.native.valid.argtypes = [ctypes.c_uint, ctypes.c_uint]
        cls.native.valid.restype = ctypes.c_int
        cls.native.temperature.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_double)]
        cls.native.temperature.restype = ctypes.c_int
        cls.native.common.argtypes = [ctypes.c_uint]
        cls.native.common.restype = ctypes.c_int

    def test_original_fixture_and_calculation_provenance(self):
        fixture = ROOT / "tests/fixtures/runtime-instruction-original.csv"
        report = json.loads((ROOT / "analysis/runtime-instruction-validation.json").read_text())
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(), report["portable_golden_sha256"])
        original = ROOT.parent / "oem/upstream/zseries_0_0_119.bin"
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), report["input_sha256"])
        self.assertEqual(report["validation_cases"], 29632)
        self.assertEqual(report["temperature_cases"], 1852)
        for path, expected in report["source_sha256"].items():
            self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), expected, path)

    def test_all_opcode_and_argument_cases_match_original_instruction_results(self):
        fixture = ROOT / "tests/fixtures/runtime-instruction-original.csv"
        opcodes, count = set(), 0
        with fixture.open() as stream:
            for row in csv.DictReader(stream):
                opcode, argument = int(row["opcode"]), int(row["argument"])
                opcodes.add(opcode)
                count += 1
                with self.subTest(opcode=opcode, argument=argument):
                    self.assertEqual(self.native.valid(opcode, argument), int(row["valid"]))
                    celsius = ctypes.c_double(-42.25)
                    supported = self.native.temperature(opcode, argument, ctypes.byref(celsius))
                    if row["celsius"]:
                        self.assertEqual(supported, 1)
                        self.assertEqual(celsius.value, float(row["celsius"]))
                    else:
                        self.assertEqual(supported, 0)
                        self.assertEqual(celsius.value, -42.25)
        self.assertEqual(count, 1792)
        self.assertEqual(opcodes, set(range(60)) | {255, 0x7fffffff, 0x80000000, 0xffffffff})

    def test_common_string_range_matches_original_unsigned_guard(self):
        for value in (0, 1, 0x7fff, *range(0x8000, 0x8026), 0x8026, 0x7fffffff, 0x80000000, 0xffffffff):
            self.assertEqual(bool(self.native.common(value)), 0x8000 <= value <= 0x8025)


if __name__ == "__main__":
    unittest.main()
