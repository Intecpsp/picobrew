#!/usr/bin/env python3
"""Validate every preserved picoferm image and produce reproducible Ghidra mappings."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
ROOT = Path(__file__).resolve().parents[4]
COMPONENT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/firmware_analysis"))
from esp_image import parse_esp8266_arduino


def reports():
    manifest = json.loads((ROOT / "firmware/upstream-manifest.json").read_text())
    records = [r for r in manifest["files"] if r["destination_path"].startswith("firmware/picoferm/oem/upstream/")]
    images = []
    latest = None
    for record in records:
        path = ROOT / record["destination_path"]
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != record["sha256"] or len(raw) != record["size"]:
            raise ValueError("OEM archive identity changed: " + str(path))
        image = parse_esp8266_arduino(raw)
        image["input"] = path.name
        image["source_path"] = record["source_path"]
        images.append(image)
        if path.name == "picoferm_0_2_7.bin":
            latest = image
    if latest is None:
        raise ValueError("Pinned latest analysis image is absent")
    components = [latest] if "segments" in latest else [latest["bootloader"], latest["application"]]
    segments = [s for c in components for s in c["segments"]]
    layout = {"schema_version": 1, "input_sha256": latest["sha256"],
              "language_id": "Xtensa:LE:32:default", "compiler_spec": "default", "thumb": False,
              "segments": [{"name": "segment_%d_%s" % (i, s["memory_kind"].lower()),
                            "offset": s["file_offset"], "address": s["load_address"], "size": s["size_bytes"],
                            "read": True, "write": s["memory_kind"] in ("DRAM", "RTC_DRAM", "RTC_DATA"),
                            "execute": s["memory_kind"] in ("IROM", "IRAM", "RTC_IRAM")}
                           for i, s in enumerate(segments)],
              "entry_points": [c["entry_address"] for c in components], "labels": []}
    inventory = {"schema_version": 1, "machine": "picoferm", "source_repository": manifest["source_repository"],
                 "source_commit": manifest["source_commit"], "images": images,
                 "flash_layout_established": False, "hardware_tested": False,
                 "scope": "Complete image-container checks and virtual mappings. No bootloader/partition/flashing compatibility claim."}
    return {"image-inventory.json": inventory, "ghidra-layout.json": layout}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for name, value in reports().items():
        path = COMPONENT / "analysis" / name
        content = json.dumps(value, indent=2) + "\n"
        if args.check:
            if not path.is_file() or path.read_text() != content:
                raise SystemExit("Stale image analysis: " + str(path))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    print("Verified picoferm archive checksums, hashes and memory mappings")


if __name__ == "__main__":
    main()
