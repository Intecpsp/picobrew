#!/usr/bin/env python3
"""Validate every preserved picostill image and produce reproducible Ghidra mappings."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
ROOT = Path(__file__).resolve().parents[4]
COMPONENT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/firmware_analysis"))
from esp_image import mapped_offset, parse_esp32


def reports():
    manifest = json.loads((ROOT / "firmware/upstream-manifest.json").read_text())
    records = [r for r in manifest["files"] if r["destination_path"].startswith("firmware/picostill/oem/upstream/")]
    images = []
    latest = None
    latest_raw = None
    for record in records:
        path = ROOT / record["destination_path"]
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != record["sha256"] or len(raw) != record["size"]:
            raise ValueError("OEM archive identity changed: " + str(path))
        image = parse_esp32(raw)
        image["input"] = path.name
        image["source_path"] = record["source_path"]
        images.append(image)
        if path.name == "picostill_0_0_35.bin":
            latest = image
            latest_raw = raw
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
    inventory = {"schema_version": 1, "machine": "picostill", "source_repository": manifest["source_repository"],
                 "source_commit": manifest["source_commit"], "images": images,
                 "flash_layout_established": False, "hardware_tested": False,
                 "scope": "Complete image-container checks and virtual mappings. No bootloader/partition/flashing compatibility claim."}
    # Reviewed function pointers from application vtables and the init array.
    # The initial automatic pass missed these entry points, including the
    # constructor that initializes board calibration and the application.
    seeds=[]
    for address, pointer in ((0x400d26e8,0x3f412900),(0x400d359c,0x3f4135f4),
                             (0x400d41f0,0x3f412914),(0x400d8688,0x3f4397d0)):
        stored=struct.unpack_from("<I",latest_raw,mapped_offset(latest,pointer,4))[0]
        entry=mapped_offset(latest,address,3)
        if stored!=address or latest_raw[entry]!=0x36 or latest_raw[entry+1]&15!=1:
            raise ValueError("Reviewed application entry pointer/prologue changed")
        layout["labels"].append({"address":address,"name":"unresolved_callback_%08x"%address,"function":True})
        seeds.append({"entry":address,"pointer_address":pointer,"entry_bytes":latest_raw[entry:entry+3].hex()})
    return {"image-inventory.json": inventory, "ghidra-layout.json": layout,
            "additional-entry-evidence.json":{"input_sha256":latest["sha256"],"entries":seeds}}


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
    print("Verified picostill archive checksums, hashes and memory mappings")


if __name__ == "__main__":
    main()
