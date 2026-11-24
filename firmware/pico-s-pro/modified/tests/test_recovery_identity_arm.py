#!/usr/bin/env python3
"""Exercise the application's OTA eligibility decision for current and legacy recoveries."""
import argparse
from hashlib import sha256
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from rebuilt_arm import RebuiltArm

# Digests of previously provisioned, physically independent recovery images.
LEGACY = {
    "pico-s-pro": "c76c390034d1564d4d80ddeeae0347b2e7171c820878b44d0fce7731c359c9ce",
    "ssd1325": "c17c462edf7f9b61ee84dc1c6fb8a33d3a48d8b0007a18bfbc38cb646c387141",
    "ssd1309": "46971bb53210fb8a3086b8ff3d2e8a0d0acd2b6680b568ee2b64f504c8bbddd4",
}


def check(elf, target):
    current = {"pico-s-pro": ROOT / "firmware/standalone/pico-hub-recovery.bin"}
    for display in ("ssd1309", "ssd1325"):
        current[display] = ROOT.parents[1] / "pico-c/modified/firmware/standalone/pico-c" / display / "pico-c-recovery.bin"
    arm = RebuiltArm(elf)

    def record(m):
        m.write(m.reg(0), bytes.fromhex("01 3d 00 20 ba dc cd ab"))
        return 1

    images = {
        b"/sys/mcuimg.bin": (14956, "67e22913fd7805a84d965928198f7d8cb795af125b81f1e0c29479a84980b975"),
        b"/sys/mcuimg2.bin": (4096, "1" * 64),
    }

    def digest(m):
        size, value = images[m.cstring(m.reg(0))]
        if size > m.reg(1):
            return 0
        m.write(m.reg(2), value.encode("ascii") + b"\0")
        m.write(m.reg(3), struct.pack("<I", size))
        return 1

    arm.hook_symbol("_ZL10ota_recordPh", record)
    arm.hook_symbol("_ZL10ota_digestPKcmPcPm", digest)
    for key, path in current.items():
        image = path.read_bytes()
        stable = sha256(image).hexdigest()
        assert stable != LEGACY[key]
        for size, value, eligible in ((len(image), stable, key == target),
                                      (len(image), LEGACY[key], key == target),
                                      (len(image), "0" * 64, False),
                                      (len(image) - 1, stable, False)):
            images[b"/sys/mcuimg1.bin"] = size, value
            actual = arm.call_symbol("_ZL11ota_inspectPbPcPm",
                                     [arm.SCRATCH, arm.SCRATCH + 4, arm.SCRATCH + 76])["r0"]
            assert actual == eligible, (target, key, size, value)
    print(f"PASS: {target} ARM application accepts its stable and legacy recoveries and rejects mismatches")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--target", choices=LEGACY, required=True)
    args = parser.parse_args()
    check(args.elf, args.target)
