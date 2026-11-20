#!/usr/bin/env python3
"""Compare readable C with instructions from both original Pico C images.

GPIO and interrupt calls are recorded at explicit boundaries. This executes no
real peripherals and cannot establish appliance-level or installation safety.
"""
import ctypes as C
import importlib.util
import json
import os
from pathlib import Path
import random
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_oem import ENCODER_ROUTINES, HELPERS, IMAGE_BASE, VARIANTS, decode_envelope, read_variant, reports

# Reuse the project's bounded ARM execution harness, not the S/Pro firmware.
SPEC = importlib.util.spec_from_file_location("original_arm", REPO / "firmware/pico-s-pro/reverse-engineered/tools/original_arm.py")
ARM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ARM)


class Encoder(C.Structure):
    _fields_ = [("phase_a_pin", C.c_uint8), ("phase_b_pin", C.c_uint8), ("button_pin", C.c_uint8),
                ("reserved_03", C.c_uint8 * 9), ("click_latch", C.c_uint8), ("reserved_0d", C.c_uint8 * 3),
                ("minimum", C.c_int32), ("maximum", C.c_int32), ("previous_position", C.c_int32),
                ("step", C.c_int32), ("position", C.c_int32), ("button_interrupt", C.c_uint8),
                ("phase_a_interrupt", C.c_uint8), ("phase_b_interrupt", C.c_uint8), ("reserved_27", C.c_uint8),
                ("wrap_enabled", C.c_uint8), ("previous_phase", C.c_uint8), ("current_phase", C.c_uint8),
                ("last_stable_phase", C.c_uint8)]


Read = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.c_uint32)
Mode = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32)
Interrupt = C.CFUNCTYPE(None, C.c_void_p)
Attach = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_int, C.c_uint32)


class Ops(C.Structure):
    _fields_ = [("digital_read", Read), ("pin_mode", Mode), ("interrupts_disable", Interrupt),
                ("interrupts_enable", Interrupt), ("attach_interrupt", Attach)]


class RecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="pico-c-reference-")
        cls.work = Path(cls.temporary.name)
        compiler = shlex.split(os.environ.get("CC", "")) or [shutil.which("cc") or shutil.which("gcc") or "cc"]
        library = cls.work / ("recovered.dll" if os.name == "nt" else "recovered.so")
        command = compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared"]
        if os.name != "nt":
            command.append("-fPIC")
        subprocess.run(command + ["-I", str(ROOT / "include"), str(ROOT / "src/encoder.c"),
                                   str(ROOT / "src/network_parse.c"), "-o", str(library)], check=True)
        cls.library = C.CDLL(str(library))
        cls.images = {}
        for variant in VARIANTS:
            path = cls.work / (variant + ".bin")
            path.write_bytes(read_variant(variant))
            cls.images[variant] = ARM.ArmFirmware(path, verify=False)
        for name, _, _ in ENCODER_ROUTINES:
            fn = getattr(cls.library, "pico_c_encoder_" + name)
            if name in ("get_position", "take_click"):
                fn.restype = C.c_uint32
            else:
                fn.restype = None
        cls.library.pico_c_parse_ipv4_address.restype = C.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_preserved_archive_and_analysis_identity(self):
        for name, report in reports().items():
            expected = (json.dumps(report, indent=2, ensure_ascii=True) + "\n").encode("ascii")
            self.assertEqual((ROOT / "analysis" / name).read_bytes(), expected)
        for variant, definition in VARIANTS.items():
            original = (ROOT.parent / "oem/upstream" / definition["file"]).read_bytes()
            corrupted = bytearray(original)
            corrupted[-3] = ord("f") if corrupted[-3] != ord("f") else ord("e")
            with self.assertRaises(ValueError):
                decode_envelope(bytes(corrupted))
            for malformed in [original[:-1], original + b"\n", original.replace(b"|", b"/", 1)]:
                with self.assertRaises(ValueError):
                    decode_envelope(malformed)

    def encoder(self, seed):
        rng = random.Random(seed)
        encoder = Encoder.from_buffer_copy(bytes(rng.getrandbits(8) for _ in range(C.sizeof(Encoder))))
        encoder.minimum, encoder.maximum = -50, 50
        encoder.position = rng.randint(-100, 100)
        return encoder

    def compare(self, variant, name, encoder, arguments=(), phases=(0, 0)):
        machine = self.images[variant]
        machine.hooks.clear()
        helpers = HELPERS[variant]
        original_events, native_events = [], []
        original_reads, native_reads = iter(phases), iter(phases)

        def read_original(arm):
            original_events.append(("read", arm.reg(0)))
            return next(original_reads)

        machine.hook(helpers["digital_read"], read_original)
        machine.hook(helpers["pin_mode"], lambda arm: original_events.append(("mode", arm.reg(0), arm.reg(1))))
        machine.hook(helpers["interrupts_disable"], lambda arm: original_events.append(("disable",)))
        machine.hook(helpers["interrupts_enable"], lambda arm: original_events.append(("enable",)))
        machine.hook(helpers["attach_interrupt"], lambda arm: original_events.append(("attach", arm.reg(0), arm.reg(1), arm.reg(2))))
        def native_read(_, pin):
            native_events.append(("read", pin))
            return next(native_reads)
        ops = Ops(Read(native_read), Mode(lambda _, pin, mode: native_events.append(("mode", pin, mode))),
                  Interrupt(lambda _: native_events.append(("disable",))), Interrupt(lambda _: native_events.append(("enable",))),
                  Attach(lambda _, pin, kind, mode: native_events.append(("attach", pin, helpers["rotation_handler" if kind == 0 else "button_handler"], mode))))
        start = next(address for routine, address, _ in ENCODER_ROUTINES if routine == name) + (4 if variant == "ssd1309" else 0)
        machine.write(machine.SCRATCH, bytes(encoder))
        arm_arguments = [machine.SCRATCH] + list(arguments)
        result = machine.call(start, arm_arguments[:4], stack_args=arm_arguments[4:], max_instructions=1000)
        native_arguments = [C.byref(encoder)] + list(arguments)
        if name in ("attach", "initialize", "handle_rotation"):
            native_arguments += [C.byref(ops), None]
        native_result = getattr(self.library, "pico_c_encoder_" + name)(*native_arguments)
        self.assertEqual(machine.read(machine.SCRATCH, C.sizeof(Encoder)), bytes(encoder), (variant, name, arguments))
        self.assertEqual(original_events, native_events, (variant, name))
        if name in ("get_position", "take_click"):
            self.assertEqual(result["r0"], native_result, (variant, name))

    def test_encoder_fields_clamping_wrapping_and_latches(self):
        for variant in VARIANTS:
            for index in range(40):
                for name in ["construct", "set_step", "set_range", "enable_wrap", "disable_wrap", "set_position", "get_position", "take_click", "clear_click"]:
                    encoder = self.encoder(index)
                    encoder.click_latch = index % 3
                    encoder.wrap_enabled = index % 2
                    args = (-index,) if name in ("set_step", "set_position") else (-25, 23, index - 20) if name == "set_range" else ()
                    self.compare(variant, name, encoder, args)
            for minimum, maximum, position in [(20, -20, 0), (-2**31, 2**31-1, -2**31), (-2**31, 2**31-1, 2**31-1)]:
                for wrap in [0, 1, 255]:
                    encoder = self.encoder(9)
                    encoder.minimum, encoder.maximum, encoder.position = minimum, maximum, position
                    encoder.wrap_enabled = wrap
                    self.compare(variant, "get_position", encoder)

    def test_encoder_gpio_interrupt_order_and_quadrature(self):
        for variant in VARIANTS:
            for phase_a in [0, 1]:
                for phase_b in [0, 1]:
                    encoder = self.encoder(1)
                    self.compare(variant, "initialize", encoder, (3, 4, 5, 6, 7, 8), (phase_a, phase_b))
                    self.compare(variant, "attach", self.encoder(2))
                    for previous in range(4):
                        for stable in range(4):
                            for position in [-2**31, -51, 0, 51, 2**31-1]:
                                encoder = self.encoder(3)
                                encoder.previous_phase, encoder.last_stable_phase = previous, stable
                                encoder.position = position
                                encoder.wrap_enabled = previous % 2
                                self.compare(variant, "handle_rotation", encoder, phases=(phase_a, phase_b))

    def test_ipv4_valid_malformed_and_original_partial_write_behavior(self):
        cases = [b"", b"0", b"1.2.3", b"1.2.3.4", b"255.255.255.255", b"256.1.2.3", b"1.2.256.4", b"...", b".1.2.", b"1.2.3.4.5", b"-1.2.3.4", b" 1.2.3.4", b"0001.002.03.4", b"1.2.3.4 ", b"\xff.2.3.4"]
        rng = random.Random(817)
        cases += [f"{rng.randrange(300)}.{rng.randrange(300)}.{rng.randrange(300)}.{rng.randrange(300)}".encode() for _ in range(100)]
        for variant, machine in self.images.items():
            machine.hooks.clear()
            address = 0x200069E0 if variant == "ssd1325" else 0x200069C8
            for text in cases:
                initial = b"\x11\x22\x33\x44\xaa\xbb\xcc\xdd"
                machine.write(machine.SCRATCH, initial)
                machine.write(machine.SCRATCH + 0x100, text + b"\0")
                result = machine.call(address, [machine.SCRATCH + 0x100, machine.SCRATCH], max_instructions=2000)
                native = C.create_string_buffer(initial, len(initial))
                returned = self.library.pico_c_parse_ipv4_address(C.c_char_p(text), C.byref(native))
                self.assertEqual(result["r0"], returned, (variant, text))
                self.assertEqual(machine.read(machine.SCRATCH, 8), bytes(native), (variant, text))


if __name__ == "__main__":
    unittest.main(verbosity=2)
