"""Fingerprint the possible AVR boot regions in a Zymatic flash backup."""

import argparse
from hashlib import sha256
import json
from pathlib import Path

import usb_stock as stock

# ATmega1284P data sheet, table 27-7. These are byte counts, not word counts.
FLASH_BYTES = 128 * 1024
BOOT_BYTES = (8192, 4096, 2048, 1024)
# Upstream Sanguino 1.0.2 HEX files at commit 5c8130f. Hash the address-aware
# 1024 bytes at 0x1fc00, with gaps filled with 0xff. Provenance in recovery.md.
REFERENCE_IMAGES = {
    (1024, "e277160f4bec16ad786ed3df9cdb1bd51b56e1b766d99f9e4ac4f09d392124ef"):
        "Sanguino 1.0.2 optiboot_atmega1284p.hex",
    (1024, "4d06a89aef0cf2593f4ebb6f6be973e2a6645d16ef68df683756417a9d32cf00"):
        "Sanguino 1.0.2 optiboot_atmega1284p_8m.hex",
}


def describe(flash):
    if len(flash) != FLASH_BYTES:
        raise ValueError("Supply a complete 131072-byte AVR flash capture")
    windows = []
    for size in BOOT_BYTES:
        data = flash[-size:]
        digest = sha256(data).hexdigest()
        if data == b"\xff" * size:
            pattern = "all-ff"
        elif data == b"\x00" * size:
            pattern = "all-00"
        elif data == data[:256] * (size // 256):
            pattern = "repeated-page"
        else:
            pattern = "mixed"
        windows.append({
            "start_byte": FLASH_BYTES - size, "size_bytes": size,
            "sha256": digest, "read_pattern": pattern,
            "matching_reference": REFERENCE_IMAGES.get((size, digest)),
        })
    return {
        "schema_version": 1, "mcu": "ATmega1284P",
        "flash_sha256": sha256(flash).hexdigest(),
        "stock_application_1_1_14": stock.is_original_app(flash[:stock.APP_SIZE]),
        "possible_boot_regions": windows,
    }


def as_json(flash):
    return (json.dumps(describe(flash), indent=2) + "\n").encode("ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="Private full-flash backup from usb-backup")
    args = parser.parse_args()
    try:
        flash = stock.private_files.read_regular(args.capture, FLASH_BYTES, private=True)
        print(as_json(flash).decode("ascii"), end="")
    except (ValueError, OSError) as exc:
        parser.exit(1, str(exc) + "\n")
