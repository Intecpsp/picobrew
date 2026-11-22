#!/usr/bin/env python3
"""Bounded, offline readers for preserved Espressif firmware containers.

The readers validate application bytes and describe memory mappings. They do
not establish a board identity, a full flash layout, or installation support.
The segment/checksum formats follow Espressif's esptool image readers; the
Arduino ESP8266 combined form is an eboot image, 0xaa sector padding, and an
application image at file offset 0x1000. No device access is performed.
"""
from __future__ import annotations

import hashlib
import struct

MAX_IMAGE_BYTES = 4 * 1024 * 1024


class ImageError(ValueError):
    """The bytes do not form one complete supported image."""


def _kind(address: int, chip: str) -> str:
    regions = {
        "esp32": ((0x3F400000, 0x3F800000, "DROM"),
                  (0x3FF80000, 0x3FF82000, "RTC_DRAM"),
                  (0x3FFB0000, 0x40000000, "DRAM"),
                  (0x40080000, 0x400C0000, "IRAM"),
                  (0x400C0000, 0x400C2000, "RTC_IRAM"),
                  (0x400D0000, 0x40400000, "IROM"),
                  (0x50000000, 0x50002000, "RTC_DATA")),
        "esp8266": ((0x3FFE0000, 0x40000000, "DRAM"),
                    (0x40100000, 0x40110000, "IRAM"),
                    (0x40200000, 0x40300000, "IROM")),
    }
    for lower, upper, name in regions[chip]:
        if lower <= address < upper:
            return name
    return "UNKNOWN"


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _bounds(data: bytes, minimum: int):
    if not minimum <= len(data) <= MAX_IMAGE_BYTES:
        raise ImageError("image length is outside the supported bounds")


def _header(data: bytes, offset: int) -> dict:
    if offset < 0 or offset + 8 > len(data):
        raise ImageError("truncated image header")
    magic, count, mode, size_frequency, entry = struct.unpack_from("<BBBBI", data, offset)
    if magic != 0xE9 or not 1 <= count <= 16:
        raise ImageError("invalid image magic or segment count")
    if mode > 3:
        raise ImageError("unsupported flash mode")
    return {"segment_count": count, "flash_mode": mode,
            "flash_size_frequency_header": size_frequency, "entry_address": entry}


def _segments(data: bytes, position: int, count: int, chip: str) -> tuple:
    checksum, segments, occupied = 0xEF, [], []
    for index in range(count):
        if position + 8 > len(data):
            raise ImageError("truncated segment header")
        address, size = struct.unpack_from("<II", data, position)
        start, end = position + 8, position + 8 + size
        if end > len(data) or address + size > 0x100000000:
            raise ImageError("truncated or overflowing segment")
        if size and any(address < high and low < address + size for low, high in occupied):
            raise ImageError("overlapping memory segments")
        if size:
            occupied.append((address, address + size))
        payload = data[start:end]
        for value in payload:
            checksum ^= value
        segments.append({"index": index, "header_offset": position,
                         "file_offset": start, "load_address": address,
                         "size_bytes": size, "memory_kind": _kind(address, chip),
                         "sha256": _sha(payload)})
        position = end
    return position, checksum, segments


def _footer(data: bytes, position: int, checksum: int) -> int:
    checksum_offset = ((position + 16) // 16) * 16 - 1
    if checksum_offset >= len(data):
        raise ImageError("truncated checksum footer")
    if any(data[position:checksum_offset]):
        raise ImageError("nonzero checksum-footer padding")
    if data[checksum_offset] != checksum:
        raise ImageError("segment XOR checksum mismatch")
    return checksum_offset


def _entry(header: dict, segments: list, bootloader=False):
    address = header["entry_address"]
    if not any((s["memory_kind"] in ("IRAM", "IROM", "RTC_IRAM") or
                bootloader and 0x40070000 <= address < 0x40080000) and
               s["load_address"] <= address < s["load_address"] + s["size_bytes"]
               for s in segments):
        raise ImageError("entry point is not in an executable segment")


def parse_esp32(data: bytes, *, bootloader=False) -> dict:
    """Read the original ESP32 (chip ID zero) application image format."""
    _bounds(data, 24)
    header = _header(data, 0)
    if data[23] not in (0, 1) or struct.unpack_from("<H", data, 12)[0] != 0:
        raise ImageError("unsupported chip ID or appended-digest flag")
    position, checksum, segments = _segments(data, 24, header["segment_count"], "esp32")
    checksum_offset = _footer(data, position, checksum)
    digest_offset = checksum_offset + 1
    if len(data) != digest_offset + (32 if data[23] else 0):
        raise ImageError("truncated digest or unexpected trailing bytes")
    digest = hashlib.sha256(data[:digest_offset]).digest()
    if data[23] and data[digest_offset:] != digest:
        raise ImageError("appended SHA-256 mismatch")
    _entry(header, segments, bootloader)
    return {"format": "Espressif ESP32 bootloader image" if bootloader else "Espressif ESP32 application image", "architecture": "Xtensa LX6",
            "size_bytes": len(data), "sha256": _sha(data), **header,
            "segments": segments, "checksum_offset": checksum_offset,
            "checksum": checksum, "checksum_verified": True,
            "appended_sha256": digest.hex() if data[23] else None,
            "digest_verified": bool(data[23]), "publisher_signature_verified": False,
            "flash_configuration_is_hardware_identification": False,
            "extended_header_hex": data[8:24].hex()}


def _esp8266_component(data: bytes, offset: int) -> dict:
    header = _header(data, offset)
    position, checksum, segments = _segments(data, offset + 8, header["segment_count"], "esp8266")
    checksum_offset = _footer(data, position, checksum)
    _entry(header, segments)
    return {"file_offset": offset, **header, "segments": segments,
            "end_offset": checksum_offset + 1, "checksum_offset": checksum_offset,
            "checksum": checksum, "checksum_verified": True}


def parse_esp8266_arduino(data: bytes) -> dict:
    """Read the eboot + 4-KiB aligned Arduino application form in PicoFerm.

    The application segment records explicitly map IROM; this reader never
    infers its destination from a filename or an unverified flash capacity.
    """
    _bounds(data, 0x1008)
    boot = _esp8266_component(data, 0)
    if boot["end_offset"] > 0x1000:
        raise ImageError("eboot exceeds its first sector")
    if any(value != 0xAA for value in data[boot["end_offset"]:0x1000]):
        raise ImageError("unexpected Arduino eboot sector padding")
    application = _esp8266_component(data, 0x1000)
    if application["end_offset"] != len(data):
        raise ImageError("unexpected trailing bytes after Arduino application")
    occupied = []
    for component in [boot, application]:
        for segment in component["segments"]:
            low, size = segment["load_address"], segment["size_bytes"]
            if size and any(low < high and begin < low + size for begin, high in occupied):
                raise ImageError("overlapping eboot and application memory")
            if size:
                occupied.append((low, low + size))
    return {"format": "Arduino ESP8266 eboot and application image",
            "architecture": "Xtensa LX106", "size_bytes": len(data), "sha256": _sha(data),
            "bootloader": boot, "application": application,
            "checksum_verified": True, "digest_verified": False,
            "publisher_signature_verified": False,
            "flash_configuration_is_hardware_identification": False}


def mapped_offset(image: dict, address: int, size: int = 1) -> int:
    if size < 0 or address < 0 or address + size > 0x100000000:
        raise ImageError("invalid requested mapped range")
    segments = image.get("segments")
    if segments is None:
        segments = [s for name in ("bootloader", "application") for s in image[name]["segments"]]
    for segment in segments:
        start = segment["load_address"]
        if start <= address and address + size <= start + segment["size_bytes"]:
            return segment["file_offset"] + address - start
    raise ImageError(f"unmapped address range 0x{address:08x}+{size}")
