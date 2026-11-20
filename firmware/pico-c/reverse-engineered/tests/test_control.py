#!/usr/bin/env python3
"""Compare typed Pico C actuator/sensor modules against both original images.

Only peripheral and UI-service boundaries are hooked. Original sensor math,
GPIO sequencing and filter predicates execute as ARM instructions. This is a
bounded software comparison, not a complete appliance or hardware test.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_oem import HELPERS, IMAGE_BASE, VARIANTS, read_variant

SPEC = importlib.util.spec_from_file_location("original_arm", REPO / "firmware/pico-s-pro/reverse-engineered/tools/original_arm.py")
ARM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ARM)


class OriginalC(ARM.ArmFirmware):
    """Bounded harness with callbacks only at observed boundaries, no math substitution."""
    def __init__(self, variant):
        raw = read_variant(variant)
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0x20000000, 0x100000)
        self.uc.mem_write(IMAGE_BASE, raw)
        self.uc.mem_write(self.RETURN, b"\x00\xbe")
        self.hooks, self.handles = {}, {}
        self.trace, self.instructions, self.returned = [], 0, False
        self.uc.hook_add(UC_HOOK_CODE, self._instruction, begin=self.RETURN, end=self.RETURN)

    def hook(self, address, callback):
        address &= ~1
        if address not in self.handles:
            self.handles[address] = self.uc.hook_add(UC_HOOK_CODE, self._instruction, begin=address, end=address)
        self.hooks[address] = callback


ACTUATORS = [
    ("pico_actuators_initialize", 0x20006254),
    ("pico_step_filter_is_inserted", 0x200050B8),
    ("pico_actuators_all_off", 0x20006310),
    ("pico_actuators_off_except_drain", 0x20006336),
    ("pico_steam_and_drain_pumps_off", 0x200062B2),
    ("pico_heater_on", 0x200062C8), ("pico_heater_off", 0x200062D0),
    ("pico_steam_pump_on", 0x2000C18C), ("pico_steam_pump_off", 0x2000629A),
    ("pico_drain_pump_on", 0x2000C1B8), ("pico_drain_pump_off", 0x200062A2),
    ("pico_shuttle_pump_on", 0x2000C34C), ("pico_shuttle_pump_off", 0x200062AA),
    ("pico_drain_pump_is_on", 0x20006354),
    ("pico_start_drain_and_steam_pumps", 0x2000C4F4),
    ("pico_pause_outputs_for_missing_filter", 0x2000C378),
]
BOUNDARIES = {
    "ssd1325": {"digital_write": 0x20017F40, "delay_us": 0x20017D64, "delay_ms": 0x20017D4C,
                "pause": 0x2000C156, "prompt": 0x2000BE00, "analog_read": 0x20017DDC},
    "ssd1309": {"digital_write": 0x20017E9C, "delay_us": 0x20017CC0, "delay_ms": 0x20017CA8,
                "pause": 0x2000C13E, "prompt": 0x2000BDE8, "analog_read": 0x20017D38},
}
PHASE_ADDRESS = 0x2002BAA2
SESSION_ADDRESS = 0x2002A8E0


def compile_library(work, name, sources, math=False):
    compiler = shlex.split(os.environ.get("CC", "")) or [shutil.which("cc") or shutil.which("gcc") or "cc"]
    path = work / (name + (".dll" if os.name == "nt" else ".so"))
    command = compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared"]
    if os.name != "nt":
        command.append("-fPIC")
    if math:
        command += ["-fno-fast-math", "-ffp-contract=off", "-fexcess-precision=standard"]
    command += ["-I", str(ROOT / "include")] + [str(ROOT / source) for source in sources]
    if math:
        command.append("-lm")
    subprocess.run(command + ["-o", str(path)], check=True)
    return ctypes.CDLL(str(path))


def actuator_cases(variant, native):
    original = OriginalC(variant)
    events, readings, read_index, restored = [], [0, 0], [0], [False]
    def read_pin(runner):
        value = 0 if restored[0] else readings[min(read_index[0], 1)]
        read_index[0] += 1
        events.append([1, runner.reg(0), value])
        return value
    def restore_filter(event_id):
        def callback(_):
            events.append([event_id, 0, 0])
            restored[0] = True
        return callback
    helpers, boundaries = HELPERS[variant], BOUNDARIES[variant]
    original.hook(helpers["digital_read"], read_pin)
    original.hook(boundaries["digital_write"], lambda r: events.append([2, r.reg(0), r.reg(1)]))
    original.hook(helpers["pin_mode"], lambda r: events.append([3, r.reg(0), r.reg(1)]))
    original.hook(boundaries["delay_us"], lambda r: events.append([4, r.reg(0), 0]))
    original.hook(boundaries["delay_ms"], lambda r: events.append([5, r.reg(0), 0]))
    original.hook(boundaries["pause"], restore_filter(6))
    original.hook(boundaries["prompt"], restore_filter(7))
    cases = []
    for operation in range(len(ACTUATORS)):
        phases = [0, 1, 4, 8, 255] if operation in [7, 9, 11, 14, 15] else [4]
        patterns = [(0, 0), (1, 0), (1, 1)] if operation in [1, 7, 9, 11, 13, 14, 15] else [(0, 0)]
        masks = range(8) if operation == 15 else [0]
        cases.extend((operation, phase, first, second, mask) for phase in phases
                     for first, second in patterns for mask in masks)
    native.pico_test_actuator_configure.argtypes = [ctypes.c_uint32] * 3
    native.pico_test_actuator_configure.restype = None
    native.pico_test_actuator_run.argtypes = [ctypes.c_uint32] * 2
    native.pico_test_actuator_run.restype = ctypes.c_uint32
    native.pico_test_actuator_event_count.restype = ctypes.c_uint32
    native.pico_test_actuator_event_word.argtypes = [ctypes.c_uint32] * 2
    native.pico_test_actuator_event_word.restype = ctypes.c_uint32
    delta = -0x18 if variant == "ssd1309" else 0
    traces = []
    for operation, phase, first, second, mask in cases:
        events.clear()
        read_index[0], restored[0] = 0, False
        readings[:] = [first, second]
        original.write(PHASE_ADDRESS, bytes([phase]))
        args = [int(bool(mask & 1)), int(bool(mask & 2)), int(bool(mask & 4))] if operation == 15 else []
        # The filter predicate is in the earlier +4 display/encoder region.
        address = ACTUATORS[operation][1] + (4 if variant == "ssd1309" and operation == 1 else delta)
        expected_return = original.call(address, args=args, max_instructions=5000)["r0"]
        expected_events = list(events)
        native.pico_test_actuator_configure(phase, first, second)
        actual_return = native.pico_test_actuator_run(operation, mask)
        actual_events = [[native.pico_test_actuator_event_word(i, j) for j in range(3)]
                         for i in range(native.pico_test_actuator_event_count())]
        if actual_events != expected_events:
            raise AssertionError((variant, operation, phase, first, second, mask, actual_events, expected_events))
        if operation in [1, 13, 15] and actual_return != expected_return:
            raise AssertionError((variant, operation, actual_return, expected_return))
        traces.append([operation, phase, first, second, mask, expected_events])
    return {"functions": len(ACTUATORS), "cases": len(cases), "mismatches": 0,
            "ordered_trace_sha256": hashlib.sha256(json.dumps(traces, separators=(",", ":")).encode()).hexdigest(),
            "meaningful_returns_checked": [ACTUATORS[index][0] for index in [1, 13, 15]]}


def sensor_cases(variant, native, quick):
    original = OriginalC(variant)
    count, events = [0], []
    original.hook(BOUNDARIES[variant]["analog_read"], lambda r: (events.append(["read", r.reg(0)]), count[0])[1])
    original.hook(HELPERS[variant]["pin_mode"], lambda r: events.append(["mode", r.reg(0), r.reg(1)]))
    results = []
    delta = -0x18 if variant == "ssd1309" else 0
    values = [0, 1, 7, 31, 127, 255, 512, 1023, 2047, 3071, 4094, 4095] if quick else range(4096)
    for pin, session, entry in [(26, 1, 0x20006A40), (25, 1, 0x20006B60), (25, 2, 0x20006B60), (25, 4, 0x20006B60)]:
        function = getattr(native, f"pico_temperature_pin{pin}_from_adc")
        function.restype = ctypes.c_float
        function.argtypes = [ctypes.c_uint32] + ([ctypes.c_int] if pin == 25 else [])
        original.write_u32(SESSION_ADDRESS, session)
        bits = bytearray()
        for adc in values:
            count[0] = adc
            events.clear()
            expected = original.call(entry + delta, max_instructions=20000)["r0"]
            actual = struct.unpack("<I", struct.pack("<f", function(*([adc, session] if pin == 25 else [adc]))))[0]
            if events != [["read", pin], ["mode", pin, 0]] or actual != expected:
                raise AssertionError((variant, pin, session, adc, hex(actual), hex(expected), events))
            bits.extend(struct.pack("<I", expected))
        results.append({"pin": pin, "session": session, "cases": len(values), "mismatches": 0,
                        "result_bits_sha256": hashlib.sha256(bits).hexdigest()})
    return {"cases": sum(group["cases"] for group in results), "groups": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true", help="Small diagnostic sensor sample; default tests all4096 counts")
    parser.add_argument("--output", type=Path, default=ROOT / "build/control-tests.json")
    args = parser.parse_args()
    report = {"schema_version": 1, "passed": False, "full_adc_domain": not args.quick,
              "physical_hardware_tested": False, "complete_firmware_tested": False,
              "scope": "Original ARM routines versus native typed C; exact binary32 sensor results and ordered peripheral/UI calls.",
              "boundaries": "GPIO/ADC reads are supplied; GPIO writes, delays and UI pause/prompt are observed hooks. No electrical behavior or interrupt concurrency is simulated.",
              "variants": {}}
    with tempfile.TemporaryDirectory(prefix="pico-c-control-") as directory:
        actuators = compile_library(Path(directory), "actuators", ["src/actuators.c", "tests/actuator_fixture.c"])
        sensors = compile_library(Path(directory), "sensors", ["src/sensors.c"], math=True)
        for variant in VARIANTS:
            report["variants"][variant] = {"payload_sha256": VARIANTS[variant]["raw_sha256"],
                "actuators": actuator_cases(variant, actuators), "sensors": sensor_cases(variant, sensors, args.quick)}
            print(f"{variant}: actuator sequencing and sensor results match original ARM", flush=True)
    report["passed"] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
