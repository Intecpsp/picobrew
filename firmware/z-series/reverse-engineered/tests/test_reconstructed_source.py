"""Compile the owned source and check it against preserved OEM evidence.

Temperature goldens originate in execution of the original instruction body,
not another copy of the reconstructed formula. Enum strings and calibration
bits are read independently from the preserved image.
"""
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import recover_enums


class ReconstructedSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = recover_enums.validated_catalog()
        cls.original = (ROOT.parent / "oem/upstream" / cls.catalog["input"]).read_bytes()
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or shutil.which(compiler[0]) is None:
            raise RuntimeError("A native C++11 compiler is required for source validation")
        cls.temporary = tempfile.TemporaryDirectory(prefix="zseries-source-")
        cls.addClassCleanup(cls.temporary.cleanup)
        build = Path(cls.temporary.name)
        enum_cases = "\n".join(
            f"case {i}: return to_string(static_cast<{entry['name']}>(value));"
            for i, entry in enumerate(cls.catalog["enums"])
        )
        bridge = r'''
#include "zseries/temperature.hpp"
#include "zseries/hex_detection.hpp"
#include <cstring>
#include <limits>
using namespace zseries::oem;
extern "C" {
const char* enum_name(unsigned which, unsigned value) {
    switch (which) { ENUM_CASES default: return nullptr; }
}
int calibration(unsigned type, double* coefficients) {
    const auto* p = thermistor_parameters(static_cast<TempSensorType>(type));
    if (!p) return 0;
    coefficients[0] = p->a; coefficients[1] = p->b;
    coefficients[2] = p->c; coefficients[3] = p->series_resistance_ohms;
    return static_cast<unsigned>(p->type) == type ? 1 : -1;
}
double temperature(unsigned type, int raw, int celsius) {
    const auto* p = thermistor_parameters(static_cast<TempSensorType>(type));
    if (!p) return std::numeric_limits<double>::quiet_NaN();
    return celsius ? thermistor_celsius(*p, raw) : thermistor_kelvin(*p, raw);
}
int valid_raw(int raw) { return raw_adc_valid(raw); }
int decode(const unsigned char* bytes, std::size_t size, int* out) {
    AdcChannels values; values.fill(123);
    const bool success = decode_max11613_reply(bytes, size, values);
    for (unsigned i = 0; i < values.size(); ++i) out[i] = values[i];
    return success;
}
int boiler_sensor(unsigned boiler) {
    TempSensorType sensor = TempSensorType::MURATA_NCU18;
    if (!hex_sensor_for_boiler(static_cast<BoilerType>(boiler), sensor))
        return sensor == TempSensorType::MURATA_NCU18 ? -1 : -2;
    return static_cast<unsigned>(sensor);
}
int detect_temperatures(const double* values, unsigned* sensor) {
    const HexTemperatures temperatures = {values[0], values[1], values[2], values[3], values[4]};
    HexDetection out = {HexDetectError::BothHexValid, TempSensorType::MURATA_NCU18};
    const bool valid = evaluate_hex_detection(temperatures, out);
    *sensor = static_cast<unsigned>(out.sensor);
    if (!valid) return out.error == HexDetectError::BothHexValid ? -1 : -2;
    return static_cast<int>(out.error);
}
int detect_adc(const int* values, unsigned* sensor) {
    const AdcChannels raw = {{values[0], values[1], values[2], values[3]}};
    HexDetection out = {HexDetectError::BothHexValid, TempSensorType::MURATA_NCU18};
    const bool valid = detect_hex_from_adc(raw, out);
    *sensor = static_cast<unsigned>(out.sensor);
    if (!valid) return out.error == HexDetectError::BothHexValid ? -1 : -2;
    return static_cast<int>(out.error);
}
}
'''.replace("ENUM_CASES", enum_cases)
        source = build / "bridge.cpp"
        source.write_text(bridge)
        library = build / "source.so"
        subprocess.run(compiler + [
            "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fno-fast-math", "-ffp-contract=off", "-fPIC", "-shared",
            "-I", str(ROOT / "include"), str(source), str(ROOT / "src/enums.cpp"),
            str(ROOT / "src/temperature.cpp"), str(ROOT / "src/hex_detection.cpp"), "-o", str(library),
        ], check=True, capture_output=True, text=True)
        cls.native = ctypes.CDLL(str(library))
        cls.native.enum_name.argtypes = [ctypes.c_uint, ctypes.c_uint]
        cls.native.enum_name.restype = ctypes.c_char_p
        cls.native.calibration.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_double)]
        cls.native.calibration.restype = ctypes.c_int
        cls.native.temperature.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_int]
        cls.native.temperature.restype = ctypes.c_double
        cls.native.valid_raw.argtypes = [ctypes.c_int]
        cls.native.valid_raw.restype = ctypes.c_int
        cls.native.decode.argtypes = [ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t,
                                     ctypes.POINTER(ctypes.c_int)]
        cls.native.decode.restype = ctypes.c_int
        cls.native.boiler_sensor.argtypes = [ctypes.c_uint]
        cls.native.boiler_sensor.restype = ctypes.c_int
        cls.native.detect_temperatures.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint)]
        cls.native.detect_temperatures.restype = ctypes.c_int
        cls.native.detect_adc.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint)]
        cls.native.detect_adc.restype = ctypes.c_int

    def test_all_owned_enum_labels_match_the_original_pointer_tables(self):
        checked = 0
        for index, entry in enumerate(self.catalog["enums"]):
            for item in entry["values"]:
                with self.subTest(enum=entry["name"], value=item["value"]):
                    self.assertEqual(self.native.enum_name(index, item["value"]).decode(), item["text"])
                    checked += 1
        self.assertEqual(checked, 713)

    def test_enum_adapter_guards_reject_out_of_range_values(self):
        for index, entry in enumerate(self.catalog["enums"]):
            for value in (entry["count"], entry["count"] + 1, 0x7fffffff, 0x80000000, 0xffffffff):
                with self.subTest(enum=entry["name"], value=value):
                    self.assertEqual(self.native.enum_name(index, value).decode(),
                                     f"<{entry['name']}::INVALID>")

    def test_generated_source_is_current(self):
        for name, contents in recover_enums.outputs(self.catalog).items():
            self.assertEqual((ROOT / name).read_text(), contents, name)

    def test_all_calibration_bits_match_original_drom_records(self):
        for sensor in range(4):
            coefficients = (ctypes.c_double * 4)()
            self.assertEqual(self.native.calibration(sensor, coefficients), 1)
            expected = self.original[0x287f8 + 40 * sensor:0x28818 + 40 * sensor]
            self.assertEqual(bytes(coefficients), expected)
        self.assertEqual(self.native.calibration(4, (ctypes.c_double * 4)()), 0)
        self.assertEqual(self.native.calibration(0xffffffff, (ctypes.c_double * 4)()), 0)

    def test_temperature_matches_original_instruction_execution_goldens(self):
        fixture = json.loads((ROOT / "tests/fixtures/temperature-original.json").read_text())
        self.assertEqual(fixture["input_sha256"], hashlib.sha256(self.original).hexdigest())
        self.assertEqual(fixture["function"], "0x400d6ac0")
        for row in fixture["cases"]:
            with self.subTest(sensor=row["sensor_type"], raw=row["raw_adc"]):
                actual = self.native.temperature(row["sensor_type"], row["raw_adc"], 0)
                # Java Math.log and the native platform libm may differ by a
                # few ulps. This is numerical, not bit-identical libm proof.
                self.assertAlmostEqual(actual, row["kelvin"], delta=2e-12)
                actual_c = self.native.temperature(row["sensor_type"], row["raw_adc"], 1)
                self.assertAlmostEqual(actual_c, row["kelvin"] - 273.15, delta=2e-12)

    def test_adc_validity_and_clamping_cover_signed_extremes(self):
        for raw in (-0x80000000, -1, 0, 14, 15, 16, 4093, 4094, 4095, 0x7fffffff):
            with self.subTest(raw=raw):
                self.assertEqual(bool(self.native.valid_raw(raw)), 15 <= raw <= 4094)
                for sensor in range(4):
                    self.assertEqual(self.native.temperature(sensor, raw, 0),
                                     self.native.temperature(sensor, max(15, min(raw, 4094)), 0))

    def test_adc_reply_decoder_masks_status_nibbles_and_preserves_channel_order(self):
        encoded = (ctypes.c_ubyte * 8)(0xa0, 0x0f, 0xb1, 0x23, 0xcf, 0xfe, 0xdf, 0xff)
        out = (ctypes.c_int * 4)()
        self.assertEqual(self.native.decode(encoded, 8, out), 1)
        self.assertEqual(list(out), [15, 0x123, 4094, 4095])
        # All possible 12-bit values, including invalid temperature endpoints,
        # are preserved at the byte-decoder layer.
        for value in range(4096):
            sample = (ctypes.c_ubyte * 8)(*([0xf0 | (value >> 8), value & 255] * 4))
            self.assertEqual(self.native.decode(sample, 8, out), 1)
            self.assertEqual(list(out), [value] * 4)

    def test_adc_failed_transaction_discards_previous_samples(self):
        encoded = (ctypes.c_ubyte * 8)(*range(8))
        for size in (*range(8), 9, 100):
            out = (ctypes.c_int * 4)(1, 2, 3, 4)
            self.assertEqual(self.native.decode(encoded, size, out), 0)
            self.assertEqual(list(out), [-1] * 4)
        out = (ctypes.c_int * 4)(1, 2, 3, 4)
        self.assertEqual(self.native.decode(None, 8, out), 0)
        self.assertEqual(list(out), [-1] * 4)

    def test_boiler_sensor_mapping_does_not_claim_z1_z2_identity(self):
        self.assertEqual(self.native.boiler_sensor(0), 0)
        self.assertEqual(self.native.boiler_sensor(1), 3)
        for invalid in (2, 3, 0xffffffff):
            self.assertEqual(self.native.boiler_sensor(invalid), -1)

    def test_hex_detector_matches_original_instruction_execution_goldens(self):
        fixture = json.loads((ROOT / "tests/fixtures/hex-detection-original.json").read_text())
        self.assertEqual(fixture["input_sha256"], hashlib.sha256(self.original).hexdigest())
        errors = set()
        for row in fixture["cases"]:
            with self.subTest(temperatures=row["temperatures"]):
                sensor = ctypes.c_uint()
                actual = self.native.detect_temperatures((ctypes.c_double * 5)(*row["temperatures"]),
                                                         ctypes.byref(sensor))
                self.assertEqual((actual, sensor.value), (row["error"], row["sensor_type"]))
                errors.add(actual)
        self.assertEqual(errors, set(range(9)))

    def test_hex_detector_rejects_undefined_numeric_inputs_without_overwriting_result(self):
        for index in range(5):
            for invalid in (math.nan, math.inf, -math.inf, 1e300, -1e300):
                values = [20.] * 5
                values[index] = invalid
                sensor = ctypes.c_uint()
                with self.subTest(index=index, invalid=invalid):
                    self.assertEqual(self.native.detect_temperatures((ctypes.c_double * 5)(*values),
                                                                     ctypes.byref(sensor)), -1)
                    self.assertEqual(sensor.value, 2)

    def test_hex_adc_adapter_uses_original_channel_and_sensor_type_mapping(self):
        for raw in ([2048, 2048, 2048, 2048], [1000, 2000, 2100, 2200],
                    [15, 4094, 16, 4093], [2600, 1900, 1910, 2100]):
            temperatures = [self.native.temperature(2, raw[3], 1),
                            self.native.temperature(1, raw[1], 1),
                            self.native.temperature(1, raw[2], 1),
                            self.native.temperature(0, raw[0], 1),
                            self.native.temperature(3, raw[0], 1)]
            expected_sensor, actual_sensor = ctypes.c_uint(), ctypes.c_uint()
            expected = self.native.detect_temperatures((ctypes.c_double * 5)(*temperatures),
                                                       ctypes.byref(expected_sensor))
            actual = self.native.detect_adc((ctypes.c_int * 4)(*raw), ctypes.byref(actual_sensor))
            self.assertEqual((actual, actual_sensor.value), (expected, expected_sensor.value))
        for index in range(4):
            for invalid in (-0x80000000, -1, 0, 14, 4095, 0x7fffffff):
                raw = [2048] * 4
                raw[index] = invalid
                sensor = ctypes.c_uint()
                self.assertEqual(self.native.detect_adc((ctypes.c_int * 4)(*raw), ctypes.byref(sensor)), -1)
                self.assertEqual(sensor.value, 2)


if __name__ == "__main__":
    unittest.main()
