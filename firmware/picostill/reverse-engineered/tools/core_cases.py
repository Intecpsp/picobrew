#!/usr/bin/env python3
"""Deterministic inputs for independent original-instruction replay."""
import itertools
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def cases():
    rows = []
    def add(op, **values):
        rows.append({"id": "%s-%04d" % (op, len(rows)), "op": op, **values})
    for count, previous, sample in itertools.product((-1, 0, 1, 2, 7, 60, 0x7fffffff),
                                                     (80.0, 0.75, -100.0), (16.0, 0.1)):
        add("ema", count=count, previous=previous, sample=sample)
    add("ema", count=0, previous=80, sample=16, assertion_seed=0xffffffff)
    # Caller-supplied arithmetic test parameters, not claimed PicoStill calibration.
    for adc in (-1, 0, 1, 15, 16, 1000, 2048, 4094, 4095, 4096, 8190):
        add("thermistor", parameters=[0.001129148, 0.000234125, 8.76741e-8, 10000.0, 4095.0], adc=adc)
    for counts in ((0, 0), (1, 1), (2, 0), (0, 2), (60, 60), (0xffffffff, 0)):
        for index in range(5):
            for rate in (0, 0.75, 0.75000006, 1, float("nan")):
                rates = [1.0]*5
                rates[index] = rate
                add("sensors", counts=list(counts), bits=list(map(bits, rates+[25, 25, 25, 25, 10])))
    for index in range(5):
        for value in (-0.00001, 0, 16, 16.00001, 80, 80.00001, float("nan"), float("inf")):
            values = [25, 25, 25, 25, 10]
            values[index] = value
            add("sensors", counts=[2, 2], bits=list(map(bits, [1]*5+values)))
    for enabled, speed, samples, rpm in itertools.product((0, 1), (154, 155, 255), (0, 4, 5), (0, 1600, 1600.001)):
        fan = [enabled, speed, samples, bits(rpm)]
        add("fan", fans=[fan, [1, 255, 5, bits(2000)]])
        add("fan", fans=[[1, 255, 5, bits(2000)], fan])
    for temperature, pressure in itertools.product((32.1, 32.2, 32.3, 35, float("nan"), float("inf")),
                                                    (-1, 10, 10.00001, 11, float("nan"))):
        add("pump", bits=list(map(bits, [temperature, pressure, 35, 2.8, 10])))
    for error, existing in itertools.product((*range(23), 0xffffffff), (0, 5)):
        add("errors", error=error, existing=existing)
    add("errors", error=0, existing=5, assertion_seed=0xffffffff)
    for state, pressed in itertools.product(range(4), (0, 1)):
        add("button", words=[pressed, 3, state, 100], steps=[[1, 101, 1, 0], [1, 102, 0, 1], [0, 103, 0, 0]])
    add("button", words=[0, 0, 0, 0], steps=[[2, 100, 0, 0], [2, 5100, 0, 0], [2, 5101, 0, 0], [2, 5200, 1, 0]])
    add("button", words=[0, 0, 0, 0], steps=[[2, 100, 0, 0], [2, 200, 1, 0], [2, 400, 1, 0], [2, 401, 1, 0]])
    add("button", words=[0, 0, 0, 0], steps=[[2, 100, 0, 0], [2, 110, 1, 0], [2, 200, 0, 0], [2, 210, 1, 0], [2, 411, 1, 0]])
    add("button", words=[1, 0xffffffff, 2, 0xfffffff0], steps=[[1, 0xfffffff1, 0, 1], [2, 0xfffffff2, 0, 0], [2, 100, 1, 0]])
    add("button", words=[1, 2, 1, 0xffffff00], steps=[[2, 0xffffff01, 0, 0]])
    return rows


if __name__ == "__main__":
    path = ROOT / "tests/fixtures/core-inputs.json"
    path.write_text(json.dumps(cases(), indent=2, allow_nan=False)+"\n")
    print("Prepared %d original-instruction cases" % len(cases()))
