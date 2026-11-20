#!/usr/bin/env python3
"""Compare C menu interaction, interlocks, timeout and redraw call ordering.

Encoder input, clock, drawing, filter and still checks are explicit boundaries.
Rendering itself is separately compared in test_ui_graphics.py. No hardware I/O.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import sys
import tempfile

from test_control import OriginalC, compile_library

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_oem import read_variant, VARIANTS


def verify(variant, native):
    original = OriginalC(variant)
    image = read_variant(variant)
    delta = -0x18 if variant == "ssd1309" else 0
    early = 4 if variant == "ssd1309" else 0
    def literal(address):
        return struct.unpack_from("<I", image, address + delta - 0x20004000)[0]
    carousel, controller, scrolling = [literal(a) for a in (0x2000db30, 0x2000db34, 0x2000db38)]
    callback = original.SCRATCH + 0x4000
    events, settings, readings = [], [], []
    counters = [0, 0, 0]
    def event(kind, *args):
        events.append([kind, *[a & 0xffffffff for a in args], *[0] * (5 - len(args))])
    def clock(r):
        value = (settings[11] + counters[0] * settings[12]) & 0xffffffff
        counters[0] += 1
        event(1, value)
        return value
    def position(r):
        index = counters[1]
        counters[1] += 1
        value = readings[min(index, len(readings) - 1)]
        event(5, value)
        return value
    def click(r):
        counters[2] += 1
        value = int(counters[2] >= settings[13])
        event(8, value)
        return value
    correspondence = json.loads((ROOT / "analysis/function-correspondence.json").read_text())
    names = correspondence["variants"][VARIANTS[variant]["display"]]["comparison"]["candidates"]
    addresses = {entry["name"]: int(entry["target"], 16) for entry in names}
    original.hook(addresses["pico_assert_nonfatal"], lambda r: None)
    original.hook(0x20017c9c if variant == "ssd1309" else 0x20017d40, clock)
    original.hook(callback, lambda r: event(2))
    original.hook(0x2000c156 + delta, lambda r: event(3))
    original.hook(0x2000458a + early, lambda r: event(4, r.reg(1), r.reg(2), r.reg(3)))
    original.hook(0x200045ae + early, position)
    original.hook(0x200045a6 + early, lambda r: event(6, r.reg(1)))
    original.hook(0x200045f2 + early, lambda r: event(7))
    original.hook(0x200045e2 + early, click)
    original.hook(0x20004488, lambda r: event(9))
    original.hook(0x20004494, lambda r: event(10))
    original.hook(0x20009974 + delta, lambda r: event(11))
    original.hook(0x200050b8 + early, lambda r: (event(12, settings[10]), settings[10])[1])
    original.hook(0x2000d898 + delta, lambda r: (event(13, settings[14]), settings[14])[1])
    original.hook(0x2000ada0 + delta,
                  lambda r: event(14, r.reg(0), r.reg(1), r.reg(2), r.reg(3), r.read_u32(r.reg(13))))
    # The field matrix includes filter/status transitions, exact timeout limits,
    # wraparound, absent redraw callbacks, clamps and both encoder directions.
    rng = random.Random(0xc032)
    cases = []
    for index in range(240):
        first, count = rng.choice((0, 1, 3, 7)), rng.choice((1, 2, 3, 8, 22))
        initial = rng.choice((0, count - 1, count, count + 2))
        start = min(initial, count - 1) + first
        values = [start] + [max(0, min(255, start + rng.choice((-2, -1, 0, 1, 2)))) for _ in range(10)]
        cfg = [first, count, initial, index % 2, 1, (index // 2) % 2, rng.choice((0, 1, 3, 60)), 0,
               rng.choice((0, 4)), rng.choice((0, 5, 6, 9, 10, 11)), index % 3 != 0,
               rng.choice((0, 1, 1000, 0xfffffff0)), rng.choice((0, 1, 1000, 2001)),
               rng.choice((1, 2, 5)), index % 4 == 0, index % 3 != 0, 0, 0]
        cases.append((list(map(int, cfg)), values))
    traces = []
    for config, values in cases:
        events.clear()
        settings[:] = config
        readings[:] = values
        counters[:] = [0, 0, 0]
        original.write(carousel, bytes(64))
        original.write_u32(carousel + 16, callback | 1 if config[15] else 0)
        original.write(controller + 0x61, bytes([config[9], config[8]]))
        original.write(scrolling + 0x130, b"\1")
        expected = original.call(0x2000d8f0 + delta, config[:4], stack_args=config[4:7], max_instructions=50000)["r0"]
        cfg = (ctypes.c_uint32 * 18)(*config)
        positions = (ctypes.c_uint32 * len(values))(*values)
        actual = native.pico_test_menu_run(cfg, positions, len(values))
        actual_events = [[native.pico_test_menu_event(i, j) for j in range(6)]
                         for i in range(native.pico_test_menu_events())]
        enabled = original.read(scrolling + 0x130, 1)[0]
        if expected != actual or events != actual_events or enabled != native.pico_test_menu_scrolling():
            raise AssertionError((variant, config, values, "returns", expected, actual,
                                  "original events", events, "native events", actual_events))
        traces.append([expected, enabled, events[:]])
    return {"variant": variant, "input_sha256": VARIANTS[variant]["raw_sha256"], "cases": len(cases),
            "trace_sha256": hashlib.sha256(json.dumps(traces).encode()).hexdigest(),
            "original_code_executed": True, "native_source_compiled": True, "hardware_tested": False,
            "limits": ["Clock/input/drawing/filter/still boundaries scripted; complete appliance not executed"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pico-c-menu-") as directory:
        native = compile_library(Path(directory), "ui_menu", ["src/ui/menu_control.c", "tests/ui_menu_fixture.c"])
        native.pico_test_menu_run.restype = ctypes.c_uint32
        native.pico_test_menu_event.restype = ctypes.c_uint32
        reports = [verify(variant, native) for variant in VARIANTS]
    content = json.dumps({"cases": sum(r["cases"] for r in reports), "variants": reports}, indent=2) + "\n"
    if args.output:
        args.output.write_text(content)
    print(content, end="")


if __name__ == "__main__":
    main()
