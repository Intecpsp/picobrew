#!/usr/bin/env python3
"""Compare C font/viewport/OLED operations with both original ARM payloads.

Graphics and text execute unmodified original instructions. Only GPIO, SPI and
delay functions are intercepted for ordered trace comparison. No hardware I/O.
"""
import argparse
import ctypes
import hashlib
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

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_oem import VARIANTS, read_variant
from test_control import OriginalC

ENTRIES = {
    "ssd1325": {"object": 0x2002a400, "buffer": 0x200296f0, "font": 0x20026c3c,
                "write": 0x200132a8, "cell": 0x20005474, "center": 0x200054ac,
                "font_on": 0x200053fc, "font_off": 0x200053f0,
                "clear_row": 0x20009944, "clear_column": 0x2000ab3c,
                "pixel": 0x2001284c, "initialize": 0x2000540c, "invert": 0x20012adc,
                "pin_mode": 0x20017e98, "pin_write": 0x20017f40,
                "delay": 0x20017d4c, "spi_begin": 0x200133fc, "spi_transfer": 0x200134b4},
    "ssd1309": {"object": 0x2002a400, "buffer": 0x20029650, "font": 0x20026b9c,
                "write": 0x20013220, "cell": 0x20005460, "center": 0x20005498,
                "font_on": 0x20005400, "font_off": 0x200053f4,
                "clear_row": 0x2000992c, "clear_column": 0x2000ab24,
                "pixel": 0x20012834, "initialize": 0x20005410, "invert": 0x20012a88,
                "pin_mode": 0x20017df4, "pin_write": 0x20017e9c,
                "delay": 0x20017ca8, "spi_begin": 0x20013358, "spi_transfer": 0x20013410},
}


def compile_native(variant, directory):
    compiler = shlex.split(os.environ.get("CC", "")) or [shutil.which("cc") or "cc"]
    output = directory / (variant + (".dll" if os.name == "nt" else ".so"))
    command = compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared"]
    if os.name != "nt":
        command.append("-fPIC")
    command += ["-I", str(ROOT / "include"), "-DPICO_C_DISPLAY_SSD1309=" + str(int(variant == "ssd1309"))]
    command += [str(ROOT / "src/ui" / (name + ".c")) for name in
                ("graphics", "assets", "context", "encoder", "display_driver", "scrolling", "menu_render",
                 "display_helpers", "display_values")]
    command += [str(ROOT / "src/network/text.c"), str(ROOT / "tests/ui_fixture.c"), "-o", str(output)]
    subprocess.run(command, check=True)
    lib = ctypes.CDLL(str(output))
    lib.pico_test_ui_reset.argtypes = [ctypes.c_int] * 8 + [ctypes.c_uint]
    lib.pico_test_ui_run.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
    lib.pico_test_ui_framebuffer.restype = ctypes.POINTER(ctypes.c_ubyte)
    lib.pico_test_ui_state.argtypes = [ctypes.c_uint]
    lib.pico_test_ui_event.argtypes = [ctypes.c_uint, ctypes.c_uint]
    lib.pico_test_ui_event.restype = ctypes.c_uint32
    lib.pico_test_scroll_configure.argtypes = [ctypes.c_char_p] + [ctypes.c_int] * 4 + [ctypes.c_uint32] + [ctypes.c_int] * 4
    lib.pico_test_scroll_state.argtypes = [ctypes.c_uint]
    lib.pico_test_scroll_state.restype = ctypes.c_int32
    lib.pico_test_scroll_text.restype = ctypes.c_void_p
    lib.pico_test_scroll_visible.restype = ctypes.c_void_p
    lib.pico_test_values_configure.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_double, ctypes.c_int]
    return lib


def verify(variant, directory, quick=False, values_only=False):
    native = compile_native(variant, directory)
    original = OriginalC(variant)
    entries = ENTRIES[variant]
    raw = read_variant(variant)
    # Object addresses come directly from each constructor's literal pool.
    object_literal = 0x20004570 if variant == "ssd1309" else 0x2000456c
    entries = dict(entries, object=struct.unpack_from("<I", raw, object_literal - 0x20004000)[0])
    obj, framebuffer = entries["object"], entries["buffer"]
    delta = 4 if variant == "ssd1309" else 0
    scrolling = struct.unpack_from("<I", raw, 0x200050b4 + delta - 0x20004000)[0]
    redraw_address = original.SCRATCH + 0x4000
    now = [0]
    events = []
    def text_hash(value):
        result = 0
        for byte in value:
            result = (result * 33 + byte) & 0xffffffff
        return result
    def log(runner):
        events.append([8, text_hash(runner.cstring(runner.reg(0))) ^ text_hash(runner.cstring(runner.reg(1))),
                       text_hash(runner.cstring(runner.reg(2)))])
    original.hook(0x20004adc + delta, log)
    def clock(runner):
        events.append([6, now[0], 0])
        return now[0]
    original.hook(0x20017c9c if variant == "ssd1309" else 0x20017d40, clock)
    original.hook(redraw_address, lambda runner: events.append([7, 0, 0]))
    for name, kind, args in (("delay", 1, (0,)), ("pin_mode", 2, (0, 1)),
                             ("pin_write", 3, (0, 1)), ("spi_begin", 4, ()),
                             ("spi_transfer", 5, (1,))):
        def callback(runner, kind=kind, args=args):
            values = [runner.reg(index) for index in args]
            events.append([kind] + values + [0] * (2 - len(values)))
            return 0
        original.hook(entries[name], callback)

    def reset(settings):
        custom, rotation, scale, wrap, x, y, fg, bg, seed = settings
        original.call(0x20004538)
        native.pico_test_ui_reset(*settings)
        nw, nh = (128, 32 if variant == "ssd1309" else 64)
        w, h = (nh, nw) if rotation & 1 else (nw, nh)
        original.write(obj + 12, struct.pack("<hhhhHHBBBBI", w, h, x, y, fg, bg,
                                             scale, rotation, wrap, 0, entries["font"] if custom else 0))
        original.write(framebuffer, bytes((i * 37 + seed) & 255 for i in range(1024)))
        label_literal = 0x2000ad9c - (0x18 if variant == "ssd1309" else 0)
        labels = struct.unpack_from("<I", raw, label_literal - 0x20004000)[0]
        for i in range(22):
            original.write(labels + i * 22, (f"Menu item {i}".encode() + b"\0").ljust(22, b"\0"))
        events.clear()

    def check(case):
        settings, operation, a, b, c, text = case[:6]
        reset(settings)
        if len(case) > 6 and operation < 17:
            old_text, row, pixel, character, frame, deadline, enabled, tick, hold, callback = case[6]
            native.pico_test_scroll_configure(*case[6])
            original.write(scrolling, bytes(316))
            original.write_u32(scrolling, redraw_address | 1 if callback else 0)
            original.write(scrolling + 4, old_text + b"\0")
            original.write_u32(scrolling + 0x100, len(old_text))
            original.write(scrolling + 0x11c, struct.pack("<iiiiI", row, pixel, character, frame, deadline))
            original.write(scrolling + 0x130, bytes([enabled]))
            original.write(scrolling + 0x134, struct.pack("<ii", tick, hold))
        now[0] = a & 0xffffffff
        value_delta = -0x18 if variant == "ssd1309" else 0
        if operation >= 17:
            celsius, timestamp, metric, integer_metric = case[6]
            native.pico_test_values_configure(*case[6])
            now[0] = timestamp
            unit_address = struct.unpack_from("<I", raw, 0x20008128 + value_delta - 0x20004000)[0]
            recipe_address = struct.unpack_from("<I", raw, 0x20007ea8 + value_delta - 0x20004000)[0]
            original.write(unit_address, bytes([celsius]))
            original.write(recipe_address + 0x218, struct.pack("<d", metric))
            original.write_u32(recipe_address + 0x214, integer_metric)
        original.write(original.SCRATCH, text + b"\0")
        functions = {
            0: (entries["write"], [obj, a]),
            1: (entries["cell"], [a, b, original.SCRATCH]),
            2: (entries["center"], [a, original.SCRATCH]),
            3: (entries["font_on"] if a else entries["font_off"], []),
            4: (entries["clear_row"], [a]),
            5: (entries["clear_column"], [a, b]),
            6: (entries["pixel"], [obj, a, b, c]),
            7: (0x20004488, []),
            8: (0x20004494, []),
            9: (entries["initialize"], []),
            10: (entries["invert"], [obj, a]),
            11: (0x20004bd0 + delta, [scrolling, original.SCRATCH, redraw_address | 1 if a else 0]),
            12: (0x2000995c if variant == "ssd1309" else 0x20009974, []),
            14: (0x200050a8 + delta, []),
            15: (0x2000ada0 - (0x18 if variant == "ssd1309" else 0), [a, b, 0, 0]),
            16: (0x2000ae80 - (0x18 if variant == "ssd1309" else 0), [original.SCRATCH, a]),
            17: (0x2000a0d8 + value_delta, []),
            18: (0x2000571c + value_delta, []),
            19: ((0x2000a0bc if a else 0x2000a0a0) + value_delta, []),
            20: (0x2000452c, [a, original.SCRATCH]),
            21: (0x20005744 + value_delta, [a, b, c]),
            22: (0x20009ee4 + value_delta, [a, b, c]),
            23: (0x20007eb4 + value_delta, [a, b, c & 1, c >> 1]),
            24: (0x20008008 + value_delta, [*struct.unpack("<II", struct.pack("<d", a / 10.0)), b, c]),
            25: (0x20007e54 + value_delta, []),
        }
        address, args = functions[operation]
        try:
            original.call(address, args=args, stack_args=[c] if operation == 15 else [], max_instructions=2000000)
        except RuntimeError as error:
            raise AssertionError((variant, case, "original execution failed")) from error
        native.pico_test_ui_run(operation, a, b, c, text)
        expected_buffer = original.read(framebuffer, 1024)
        actual_buffer = bytes(native.pico_test_ui_framebuffer()[:1024])
        if expected_buffer != actual_buffer:
            offsets = [i for i, (e, v) in enumerate(zip(expected_buffer, actual_buffer)) if e != v]
            raise AssertionError((variant, case, "framebuffer mismatch", offsets[:20]))
        w, h, x, y, fg, bg = struct.unpack("<hhhhHH", original.read(obj + 12, 12))
        expected_state = [x, y, w, h, int(original.read_u32(obj + 28) != 0), original.read(obj + 25, 1)[0], fg, bg]
        actual_state = [native.pico_test_ui_state(i) for i in range(8)]
        if expected_state != actual_state:
            raise AssertionError((variant, case, "state mismatch", expected_state, actual_state))
        actual_events = [[native.pico_test_ui_event(i, j) for j in range(3)]
                         for i in range(native.pico_test_ui_events())]
        if events != actual_events:
            from itertools import zip_longest
            first = next((i, e, a) for i, (e, a) in enumerate(zip_longest(events, actual_events)) if e != a)
            raise AssertionError((variant, case, "ordered peripheral trace mismatch", first,
                                  len(events), len(actual_events)))
        if len(case) > 6 and operation < 17:
            expected_scroll = [int(original.read_u32(scrolling) != 0),
                               struct.unpack("<i", original.read(scrolling + 0x100, 4))[0]]
            expected_scroll += list(struct.unpack("<5i", original.read(scrolling + 0x11c, 20)))
            expected_scroll += [original.read(scrolling + 0x130, 1)[0]]
            expected_scroll += list(struct.unpack("<2i", original.read(scrolling + 0x134, 8)))
            actual_scroll = [native.pico_test_scroll_state(i) for i in range(10)]
            if expected_scroll != actual_scroll:
                raise AssertionError((variant, case, "scrolling state mismatch", expected_scroll, actual_scroll))
            for offset, size, getter in ((4, 252, native.pico_test_scroll_text),
                                        (0x104, 21, native.pico_test_scroll_visible)):
                if original.read(scrolling + offset, size) != ctypes.string_at(getter(), size):
                    raise AssertionError((variant, case, "scrolling text mismatch", offset))
        return hashlib.sha256(expected_buffer + struct.pack("<8i", *expected_state) +
                              json.dumps(events, separators=(",", ":")).encode()).hexdigest()

    base = [1, 0, 1, 1, 0, 0, 1, 0, 19]
    cases = []
    characters = [0, 10, 13, 31, *range(32, 127), 127, 175, 176, 255]
    if quick:
        characters = [10, 13, 32, 65, 126, 255]
    positions = [(-9, -5), (0, 0), (119, 0), (120, 18), (126, 31), (10, 58)]
    for custom in (0, 1):
        for rotation in range(4):
            for scale in (1, 2):
                for index, character in enumerate(characters):
                    settings = base[:]
                    settings[:3] = [custom, rotation, scale]
                    settings[4:6] = positions[index % len(positions)]
                    settings[3] = index % 2
                    cases.append((settings, 0, character, 0, 0, b""))
    for custom in (0, 1):
        for row in (-1, 0, 1, 3, 7):
            settings = base[:]
            settings[0] = custom
            for operation in (1, 2, 4, 5):
                cases.append((settings, operation, row, 2, 0, b"Pico C 0.1.34"))
        for operation, a in ((3, 0), (3, 1), (7, 0), (8, 0), (9, 0), (10, 0), (10, 1)):
            settings = base[:]
            settings[0] = custom
            cases.append((settings, operation, a, 0, 0, b""))
    rng = random.Random(0x5049434f)
    for _ in range(16 if quick else 160):
        settings = base[:]
        settings[1] = rng.randrange(4)
        cases.append((settings, 6, rng.randrange(-3, 132), rng.randrange(-3, 132), rng.randrange(3), b""))
    text_values = [b"", b"Pico C", b"A full original scrolling message that is longer than its window"]
    for old_text in text_values:
        for callback in (0, 1):
            for enabled in (0, 1):
                for now_ms, deadline in ((0, 0), (1, 0), (25, 25), (26, 25), (0xffffffff, 3), (0, 0xffffffff)):
                    for frame, pixel in ((0, 0), (20, 6), (21, 5)):
                        state = (old_text, 1, pixel, 0, frame, deadline, enabled, 25, 500, callback)
                        cases.append((base[:], 12, now_ms, 0, 0, b"", state))
            state = (old_text, 1, 3, 0, 9, 100, 1, 25, 500, callback)
            cases.append((base[:], 11, callback, 0, 0, b"New scrolling message", state))
            cases.append((base[:], 14, 0, 0, 0, b"", state))
    for custom in (0, 1):
        for rotation in (0, 2):
            settings = base[:]
            settings[:2] = [custom, rotation]
            for count in (1, 2, 22):
                for selected in sorted({0, count - 1}):
                    for direction in (-1, 0, 1):
                        cases.append((settings, 15, count, selected, direction, b""))
            for more in (0, 1):
                cases.append((settings, 16, more, 0, 0, b"Continue"))
    if values_only:
        cases = []
    for custom in (0, 1):
        for celsius in (0, 1):
            settings = base[:]
            settings[0] = custom
            state = (celsius, 65001, 5.25, 34)
            for op, a, b, c in ((17, 0, 0, 0), (18, 0, 0, 0), (19, 0, 0, 0), (19, 1, 0, 0),
                                 (20, 0, 0, 0), (20, 16, 0, 0), (21, 5, 1, 65), (25, 0, 0, 0)):
                cases.append((settings, op, a, b, c, b"Pico C", state))
            for row in (0, 1, 3):
                for duration in (0, 64, 65, 120):
                    cases.append((settings, 22, row, 0, duration, b"", state))
                for minutes, seconds in ((0, 0), (1, 9), (59, 59), (60, 10), (599, 0), (720, 59)):
                    for show in (0, 1):
                        cases.append((settings, 23, minutes, seconds, (row << 1) | show, b"", state))
                for temperature in (-50, 0, 320, 705, 1000, 2120):
                    for show in (0, 1):
                        cases.append((settings, 24, temperature, show, row, b"", state))
    traces = [check(case) for case in cases]
    return {"variant": variant, "input_sha256": VARIANTS[variant]["raw_sha256"],
            "cases": len(cases), "case_trace_sha256": hashlib.sha256("\n".join(traces).encode()).hexdigest(),
            "original_code_executed": True, "native_source_compiled": True,
            "comparisons": ["all 1024 transfer-window bytes", "cursor/viewport/font/color state", "ordered GPIO/SPI/delay effects"],
            "hardware_tested": False,
            "limits": ["Selected function domains, not whole-appliance equivalence",
                       "SSD1309 upper transfer-window bytes are supplied explicitly; original adjacent mutable state is not aliased"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--values-only", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pico-c-ui-") as temporary:
        reports = [verify(variant, Path(temporary), args.quick, args.values_only) for variant in VARIANTS]
    output = {"schema_version": 1, "variants": reports, "hardware_tested": False}
    if args.output:
        args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps({"cases": sum(r["cases"] for r in reports), "variants": reports}, indent=2))


if __name__ == "__main__":
    main()
