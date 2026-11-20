#!/usr/bin/env python3
"""Check that a native rebuild reproduces the committed standalone package."""

import hashlib
import json
import argparse
from pathlib import Path
from machine_model import add_model_argument


ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(model, display):
    built_dir = ROOT / "build/standalone" / model / display / "firmware"
    packaged_dir = ROOT / "firmware/standalone" / model / display
    built = json.loads((built_dir / "firmware-report.json").read_text(encoding="utf-8"))
    packaged = json.loads((packaged_dir / "firmware-report.json").read_text(encoding="utf-8"))
    for filename, key in (
        (model + "-standalone.bin", "binary_sha256"),
        (model + "-standalone.update.txt", "update_body_sha256"),
    ):
        actual = digest(built_dir / filename)
        # The installation package contains the raw image and records the
        # optional legacy update envelope's digest in its report.
        packaged_file = packaged_dir / filename
        if ((packaged_file.exists() and actual != digest(packaged_file))
                or actual != built[key] or actual != packaged[key]):
            raise ValueError(f"Rebuilt {filename} differs from the committed package or its report")
        if key == "binary_sha256" and not packaged_file.is_file():
            raise ValueError("The committed standalone image is missing")
    for key in (
        "profile", "model", "model_family", "display_variant", "display_geometry", "firmware_update",
        "firmware_version", "image_layout", "app_sources_sha256",
        "compiled_sources", "web_sources_sha256", "browser_storage",
    ):
        if built[key] != packaged[key]:
            raise ValueError(f"Rebuilt {key} differs from the committed package")
    if not built["sdk_link_passed"] or not built["image_layout"]["passed"]:
        raise ValueError("The rebuilt image failed its SDK or layout checks")
    manifest_hash = digest(ROOT / "firmware/web/manifest.json")
    if built["browser_manifest_sha256"] != manifest_hash or packaged["browser_manifest_sha256"] != manifest_hash:
        raise ValueError("The browser bundle differs from the rebuilt or packaged firmware")
    print("Packaged firmware, update envelope, source identities, image layout and browser bundle match the clean build.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    add_model_argument(parser)
    args = parser.parse_args()
    verify(args.model, args.display)
