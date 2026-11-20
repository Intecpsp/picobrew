#!/usr/bin/env python3
"""Verify preserved Z ESP32 containers and recover their memory/source evidence.

This is an offline reader. It does not flash, patch, execute, or contact a device.
Format reference: Espressif esptool Firmware Image Format (ESP32).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

COMPONENT = Path(__file__).resolve().parents[1]
FAMILY = COMPONENT.parent
IMAGES = {
    "zseries_0_0_116.bin": "27d9e792f6d7e589b41f29d1e3bd42828d3d1a9f315adc100b03fd53d8256097",
    "zseries_0_0_119.bin": "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27",
}
SOURCE_PREFIX = r"C:\dev\ZSeries\build\ZSeries\build\sketch" + "\\"
MAX_IMAGE_BYTES = 4 * 1024 * 1024


class ImageError(ValueError):
    """The input is not a complete supported ESP32 image container."""


def address_kind(address: int) -> str:
    if 0x3F400000 <= address < 0x3F800000:
        return "DROM"
    if 0x3FF80000 <= address < 0x3FF82000:
        return "RTC_DRAM"
    if 0x3FFB0000 <= address < 0x40000000:
        return "DRAM"
    if 0x40080000 <= address < 0x400C0000:
        return "IRAM"
    if 0x400C0000 <= address < 0x400C2000:
        return "RTC_IRAM"
    if 0x400D0000 <= address < 0x40400000:
        return "IROM"
    if 0x50000000 <= address < 0x50002000:
        return "RTC_DATA"
    return "UNKNOWN"


def parse_image(data: bytes) -> dict:
    if not 24 <= len(data) <= MAX_IMAGE_BYTES:
        raise ImageError("image length is outside the supported bounds")
    if data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise ImageError("invalid ESP image magic or segment count")
    if data[2] > 3 or data[23] not in (0, 1):
        raise ImageError("unsupported flash mode or digest flag")
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != 0:
        raise ImageError("this reader is limited to the original ESP32 chip ID")
    entry = struct.unpack_from("<I", data, 4)[0]
    position = 24
    checksum = 0xEF
    segments = []
    occupied = []
    for index in range(data[1]):
        if position + 8 > len(data):
            raise ImageError("truncated segment header")
        address, size = struct.unpack_from("<II", data, position)
        offset = position + 8
        end = offset + size
        if end > len(data) or address + size > 2**32:
            raise ImageError("truncated or overflowing segment")
        if size and any(address < upper and lower < address + size for lower, upper in occupied):
            raise ImageError("overlapping virtual-address segments")
        if size:
            occupied.append((address, address + size))
        payload = data[offset:end]
        for byte in payload:
            checksum ^= byte
        segments.append({
            "index": index,
            "header_offset": position,
            "file_offset": offset,
            "load_address": address,
            "size_bytes": size,
            "memory_kind": address_kind(address),
            "sha256": hashlib.sha256(payload).hexdigest(),
        })
        position = end
    checksum_offset = ((position + 16) // 16) * 16 - 1
    expected_length = checksum_offset + 1 + (32 if data[23] else 0)
    if expected_length != len(data):
        raise ImageError("truncated footer or unexpected trailing bytes")
    if any(data[position:checksum_offset]):
        raise ImageError("nonzero footer padding")
    if data[checksum_offset] != checksum:
        raise ImageError("segment XOR checksum mismatch")
    digest = hashlib.sha256(data[:checksum_offset + 1]).digest()
    if data[23] and data[checksum_offset + 1:] != digest:
        raise ImageError("appended SHA-256 mismatch")
    if not any(s["size_bytes"] and s["memory_kind"] in ("IROM", "IRAM") and
               s["load_address"] <= entry < s["load_address"] + s["size_bytes"] for s in segments):
        raise ImageError("entry point is not inside an executable image segment")
    return {
        "format": "Espressif ESP32 application image",
        "architecture": "Xtensa LX6, little-endian",
        "size_bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "entry_address": entry,
        "chip_id": chip_id,
        "flash_mode": data[2],
        "flash_size_frequency_header": data[3],
        "flash_configuration_is_hardware_identification": False,
        "extended_header_hex": data[8:24].hex(),
        "segments": segments,
        "checksum_offset": checksum_offset,
        "checksum": checksum,
        "appended_sha256": digest.hex() if data[23] else None,
        "checksum_verified": True,
        "digest_verified": bool(data[23]),
        "publisher_signature_verified": False,
    }


def mapped_strings(data: bytes, image: dict):
    for segment in image["segments"]:
        if segment["memory_kind"] not in ("DROM", "DRAM"):
            continue
        offset = segment["file_offset"]
        payload = data[offset:offset + segment["size_bytes"]]
        for match in re.finditer(rb"[ -~]{3,}\x00", payload):
            yield {
                "address": segment["load_address"] + match.start(),
                "file_offset": offset + match.start(),
                "text": match.group()[:-1].decode("ascii"),
            }


def source_inventory(data: bytes, image: dict) -> dict:
    strings = list(mapped_strings(data, image))
    headers = []
    for item in strings:
        # Some strings are immediately preceded by printable pointer bytes.
        start = item["text"].find(SOURCE_PREFIX)
        if start < 0:
            continue
        name = item["text"][start + len(SOURCE_PREFIX):]
        headers.append({"file": name, "address": item["address"] + start,
                        "file_offset": item["file_offset"] + start})
    enum_types = []
    for item in strings:
        match = re.fullmatch(r"<([A-Za-z0-9_]+)::INVALID>", item["text"])
        if match:
            enum_types.append({"name": match[1], "address": item["address"],
                               "file_offset": item["file_offset"]})
    versions = [item for item in strings if re.fullmatch(r"0\.0\.\d+", item["text"])]
    build_literals = [item for item in strings if re.fullmatch(
        r"(?:v\d+\.\d+[^ ]*-g[0-9a-f]+(?:-dirty)?|[0-9a-f]{40}|"
        r"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}|"
        r"[0-2][0-9]:[0-5][0-9]:[0-5][0-9]|V8\.2\.0)", item["text"])]
    # Only paths are recorded here. Original image strings include vendor test
    # credentials; this inventory deliberately does not republish URL queries.
    sdk_paths = []
    for item in strings:
        text = item["text"]
        markers = ("\\hardware\\espressif\\esp32\\", "/esp-idf-public/components/")
        for marker in markers:
            start = text.find(marker)
            if start >= 0:
                sdk_paths.append({"path_suffix": text[start:], "address": item["address"] + start,
                                  "file_offset": item["file_offset"] + start})
                break
    return {"application_headers": sorted(headers, key=lambda x: x["file"]),
            "enum_type_names": enum_types, "version_literals": versions,
            "build_literals": build_literals,
            "sdk_source_paths": sdk_paths,
            "original_names_observed": True,
            "function_behaviors_reconstructed_by_this_inventory": False}


def build_reports() -> tuple[dict, dict]:
    images = []
    inventory = []
    for name, expected in sorted(IMAGES.items()):
        data = (FAMILY / "oem" / "upstream" / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ImageError(f"preserved input changed: {name}")
        image = parse_image(data)
        image["filename"] = name
        images.append(image)
        inventory.append({"filename": name, "sha256": image["sha256"], **source_inventory(data, image)})
    common = {
        "schema_version": 1,
        "format_reference": "https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/firmware-image-format.html",
        "hardware_tested": False,
        "z1_z2_mapping_established": False,
    }
    return ({**common, "images": images}, {**common, "images": inventory})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify that committed analysis is current")
    args = parser.parse_args()
    for filename, report in zip(("images.json", "source-inventory.json"), build_reports()):
        text = json.dumps(report, indent=2, sort_keys=True) + "\n"
        output = COMPONENT / "analysis" / filename
        if args.check:
            if not output.exists() or output.read_text() != text:
                raise SystemExit(f"stale analysis: {output}")
        else:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(text)
    print("Verified 2 preserved Z-series ESP32 images, segment XOR checksums, and SHA-256 digests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
