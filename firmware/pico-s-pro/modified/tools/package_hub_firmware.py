#!/usr/bin/env python3
"""Validate and package an S/Pro image and its website for upload to the Hub.

The default inputs are the committed standalone package and browser assets; no
compiler is needed. Pass --firmware-directory to package a completed local
build. Validation checks package consistency, not hardware qualification or a
publisher signature. The existing report is included byte for byte.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile
import zipfile
import zlib

from embed_web import ASSETS, DEVICE_NAMES
from machine_model import MODELS, add_model_argument


ROOT = Path(__file__).resolve().parents[1]
IMAGE_BASE, RAM_END = 0x20004000, 0x20040000
MAX_IMAGE = 184 * 1024
MAX_REPORT, MAX_MANIFEST = 512 * 1024, 16 * 1024
MAX_GZIP, MAX_SOURCE, MAX_PACKAGE = 65024, 1024 * 1024, 2 * 1024 * 1024
ZIP_DATE = (1980, 1, 1, 0, 0, 0)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, name, minimum=0, maximum=RAM_END - IMAGE_BASE):
    require(type(value) is int and minimum <= value <= maximum,
            f"{name} must be an integer in {minimum}..{maximum}")
    return value


def address(value, name):
    require(isinstance(value, str) and re.fullmatch(r"0x[0-9a-fA-F]{1,8}", value),
            f"{name} must be a hexadecimal address")
    return int(value, 16)


def check_hash(data, expected, name):
    require(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected),
            f"{name} must declare a SHA-256 digest")
    require(hashlib.sha256(data).hexdigest() == expected, f"{name} SHA-256 mismatch")


def read_file(path, limit):
    require(stat.S_ISREG(path.lstat().st_mode), f"Expected a regular file: {path}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, f"{path.name} exceeds its {limit}-byte limit")
    return data


def json_object(data, name):
    def pairs(items):
        result = {}
        folded = set()
        for key, value in items:
            require(key.lower() not in folded, f"Duplicate JSON field (including case aliases) in {name}: {key}")
            folded.add(key.lower())
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError(f"Invalid JSON constant in {name}: {value}")

    result = json.loads(data.decode("utf-8"), object_pairs_hook=pairs,
                        parse_constant=invalid_constant)
    require(isinstance(result, dict), f"{name} must be a JSON object")

    def bounded_depth(value, depth=0):
        require(depth <= 32, f"{name} exceeds the JSON nesting limit")
        children = value.values() if isinstance(value, dict) else value if isinstance(value, list) else ()
        for child in children:
            bounded_depth(child, depth + 1)

    bounded_depth(result)
    return result


def validate_image(report, binary):
    require(report.get("sdk_link_passed") is True, "Report must record a successful SDK link")
    layout = report.get("image_layout")
    require(isinstance(layout, dict) and layout.get("passed") is True,
            "Report must record a successful image layout check")
    require(address(layout.get("load_address"), "load_address") == IMAGE_BASE and
            address(layout.get("ram_end"), "ram_end") == RAM_END, "Wrong CC3200 image RAM layout")
    length = integer(layout.get("binary_bytes"), "binary_bytes", 16, MAX_IMAGE)
    require(len(binary) == length, "Binary length differs from image_layout.binary_bytes")
    check_hash(binary, report.get("binary_sha256"), "Binary")
    sp, reset = struct.unpack_from("<II", binary)
    image_end = IMAGE_BASE + length
    require(sp % 8 == 0 and image_end + 8 <= sp <= RAM_END,
            "Invalid stack vector: stack must be aligned and above the loaded image")
    require(reset & 1 and IMAGE_BASE + 8 <= (reset & ~1) <= image_end - 2,
            "Invalid reset vector: expected a Thumb entry inside the loaded image")
    for field, value in (("initial_stack_pointer", sp), ("reset_vector", reset), ("entry", reset)):
        require(address(layout.get(field), field) == value, f"Report {field} differs from binary vectors")
    for field in ("bss_bytes", "heap_bytes", "sdk_stack_bytes", "unallocated_ram_bytes"):
        if field in layout:
            integer(layout[field], field)
    for field in ("undefined_symbols", "test_symbols"):
        require(layout.get(field) == [], f"Image layout must have no {field}")
    segments = layout.get("load_segments")
    require(isinstance(segments, list) and 1 <= len(segments) <= 16,
            "Image layout must declare 1..16 load segments")
    ranges, file_ends = [], []
    for segment in segments:
        require(isinstance(segment, dict), "Invalid image load segment")
        start = address(segment.get("start"), "load segment start")
        memory = integer(segment.get("memory_bytes"), "load segment memory_bytes", 1)
        size = integer(segment.get("file_bytes"), "load segment file_bytes", maximum=memory)
        require(IMAGE_BASE <= start and start + memory <= RAM_END and start + size <= image_end,
                "Image load segment is outside the reported image or application RAM")
        ranges.append((start, start + memory))
        if size:
            file_ends.append(start + size)
    ordered = sorted(ranges)
    require(all(left[1] <= right[0] for left, right in zip(ordered, ordered[1:])),
            "Image load segments overlap")
    require(file_ends and max(file_ends) == image_end and ordered[0][0] == IMAGE_BASE,
            "Image load segments do not match the binary extent")
    symbols = layout.get("required_entry_symbols", {})
    require(isinstance(symbols, dict), "Invalid required_entry_symbols")
    for field, expected in (("g_pfnVectors", IMAGE_BASE), ("ResetISR", reset)):
        if field in symbols:
            require(address(symbols[field], field) == expected, f"Image {field} is inconsistent")


def validate_web(report, manifest_bytes, web_directory):
    check_hash(manifest_bytes, report.get("browser_manifest_sha256"), "Browser manifest")
    manifest = json_object(manifest_bytes, "web/manifest.json")
    require(set(manifest) == {"schema_version", "block_size", "metadata_per_file", "fail_safe", "assets"},
            "Browser manifest has missing or unsupported fields")
    require(json.dumps(report.get("browser_storage"), sort_keys=True) == json.dumps(manifest, sort_keys=True),
            "Browser manifest differs from the report browser_storage")
    for field, expected in (("schema_version", 1), ("block_size", 4096), ("metadata_per_file", 512)):
        require(type(manifest.get(field)) is int and manifest[field] == expected,
                f"Unsupported browser manifest {field}")
    require(manifest.get("fail_safe") is False, "Browser manifest fail_safe must be false")
    assets = manifest.get("assets")
    require(isinstance(assets, list) and len(assets) == len(ASSETS),
            "Browser manifest must contain exactly five assets")
    entries = {"web/manifest.json": manifest_bytes}
    for asset, (source, content_type) in zip(assets, ASSETS):
        require(isinstance(asset, dict), "Invalid browser asset record")
        require(set(asset) == {"source", "file_path", "payload", "content_type", "content_encoding", "length",
                               "max_size", "allocation_blocks", "sha256", "source_length", "source_sha256"},
                "Browser asset has missing or unsupported fields")
        require(asset.get("source") == source, "Browser assets must use the generated manifest order")
        payload = source + ".gz"
        require(asset.get("payload") == payload and
                asset.get("file_path") == "/local/web/" + DEVICE_NAMES[source] and
                asset.get("content_type") == content_type and
                asset.get("content_encoding") == "gzip", f"Wrong browser asset mapping for {source}")
        size = integer(asset.get("length"), f"{payload} length", 10, MAX_GZIP)
        blocks = (size + 512 + 4095) // 4096
        require(integer(asset.get("allocation_blocks"), f"{payload} allocation_blocks", 1, 16) == blocks
                and integer(asset.get("max_size"), f"{payload} max_size", 1, MAX_GZIP) == blocks * 4096 - 512,
                f"Wrong browser allocation for {source}")
        packed = read_file(web_directory / payload, MAX_GZIP)
        require(len(packed) == size, f"{payload} length mismatch")
        check_hash(packed, asset.get("sha256"), payload)
        source_length = integer(asset.get("source_length"), f"{payload} source_length", 1, MAX_SOURCE)
        try:
            decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
            raw = decoder.decompress(packed, source_length + 1)
            require(decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail
                    and len(raw) == source_length, f"{payload} gzip source length or stream mismatch")
        except zlib.error as error:
            raise ValueError(f"{payload} is not a valid gzip stream") from error
        check_hash(raw, asset.get("source_sha256"), f"{payload} source")
        entries["web/" + payload] = packed
    return entries


def package_firmware(model, firmware_directory=None, web_directory=None, output=None):
    """Return the output Path after validating every included byte and writing atomically."""
    require(model in MODELS, "Only pico-pro and pico-s standalone packages are supported")
    firmware_directory = Path(firmware_directory) if firmware_directory is not None else ROOT / "firmware/standalone" / model
    web_directory = Path(web_directory) if web_directory is not None else ROOT / "firmware/web"
    report_bytes = read_file(firmware_directory / "firmware-report.json", MAX_REPORT)
    report = json_object(report_bytes, "firmware-report.json")
    require(report.get("profile") == "standalone" and report.get("model") == model and
            report.get("model_family") == "pico-s-pro", "Report model, family or profile differs from the requested standalone model")
    require(report.get("api_key_embedded") is False, "Report must explicitly declare api_key_embedded false")
    version = report.get("firmware_version")
    require(isinstance(version, str) and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,30}", version),
            "Firmware version must be a safe 1..31-character version identifier")
    binary_name = model + "-standalone.bin"
    require(isinstance(report.get("files"), dict) and report["files"].get("binary") == binary_name,
            "Report files.binary must name the requested model's standalone binary")
    binary = read_file(firmware_directory / binary_name, MAX_IMAGE)
    validate_image(report, binary)
    manifest_bytes = read_file(web_directory / "manifest.json", MAX_MANIFEST)
    entries = {"firmware-report.json": report_bytes, binary_name: binary}
    entries.update(validate_web(report, manifest_bytes, web_directory))
    require(len(entries) == 8 and sum(map(len, entries.values())) <= MAX_PACKAGE,
            "Package must contain exactly eight files within the package size limit")
    output = Path(output) if output is not None else ROOT / "build/standalone" / model / "firmware" / f"{model}-standalone-{version}.zip"
    require(output.suffix.lower() == ".zip", "Output must be a .zip file")
    with io.BytesIO() as buffer:
        # STORED avoids dependence on the host zlib version. Website files are
        # already gzip-compressed; all entry metadata and ordering are fixed.
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_STORED, allowZip64=False) as archive:
            for name in sorted(entries):
                info = zipfile.ZipInfo(name, ZIP_DATE)
                info.create_system = 3
                info.create_version = info.extract_version = 20
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(info, entries[name])
        data = buffer.getvalue()
    require(len(data) <= MAX_PACKAGE, "ZIP exceeds the package size limit")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".hub-package-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    add_model_argument(parser)
    parser.add_argument("--firmware-directory", type=Path,
                        help="Directory with firmware-report.json and the raw model binary (default: committed package)")
    parser.add_argument("--web-directory", type=Path,
                        help="Directory with manifest.json and five gzip assets (default: committed firmware/web)")
    parser.add_argument("--output", type=Path,
                        help="ZIP destination (default: build/standalone/<model>/firmware/<model>-standalone-<version>.zip)")
    args = parser.parse_args(argv)
    try:
        output = package_firmware(args.model, args.firmware_directory, args.web_directory, args.output)
    except (OSError, ValueError, RecursionError) as error:
        parser.error(str(error))
    print(f"Created {output} (8 files, {output.stat().st_size} bytes)")
    print("SHA-256: " + hashlib.sha256(output.read_bytes()).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
