"""Exercise the actual composed AVR firmware against the simavr ATmega1284P and simulated Ethernet."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import zymatic_build as build
import usb_stock
import compact_build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oem-file", type=Path, required=True)
    args = parser.parse_args()
    original = usb_stock.decode_application(usb_stock.unpack_source(args.oem_file.read_bytes()))
    flags = os.environ.get("SIMAVR_FLAGS")
    if not flags:
        flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "simavr"], text=True)
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        binary = directory / "synthetic-paired.bin"
        base = directory / "original.bin"
        base.write_bytes(original)
        table_harness = directory / "simavr-tables"
        subprocess.run([os.environ.get("CC", "cc"), "-O2", str(HERE / "simavr_tables.c"),
                        *shlex.split(flags), "-o", str(table_harness)], check=True)
        harness = directory / "simavr-overlay"
        subprocess.run([os.environ.get("CC", "cc"), "-O2", str(HERE / "simavr_overlay.c"),
                        *shlex.split(flags), "-o", str(harness)], check=True)
        for profile in (build, compact_build):
            image, _ = profile.build_application(original, bytes(range(32)), "0a"*16, "brewery.lan:8080")
            binary.write_bytes(image)
            _, template = profile.template()
            symbols = template["symbols"]
            command = [str(harness), str(binary), str(symbols["zymatic_hub_host"] - 0x800000),
                       str(symbols["__extension_bss_end"] - 0x800000),
                       str(symbols["zymatic_hub_authority"] - 0x800000), str(symbols["zymatic_image_invalid"])]
            cases = ["api", "corrupt", "factory-good", "factory-bad"]
            if profile == build:
                subprocess.run([str(table_harness), str(base), str(binary)], check=True, timeout=60)
                cases.extend(("display", "program-status", "program-history", "about-pairing", "program-display",
                              "menu-encoder", "dialog-encoder", "program-encoder"))
            for case in cases:
                subprocess.run([*command, case, str(base)], check=True, timeout=45)


if __name__ == "__main__":
    main()
