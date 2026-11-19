#!/usr/bin/env python3
"""Check that a native rebuild reproduces the committed standalone package."""

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify():
    built_dir = ROOT / "build/standalone/firmware"
    packaged_dir = ROOT / "firmware/standalone"
    built = json.loads((built_dir / "firmware-report.json").read_text(encoding="utf-8"))
    packaged = json.loads((packaged_dir / "firmware-report.json").read_text(encoding="utf-8"))
    for filename, key in (
        ("pico-s-pro-standalone.bin", "binary_sha256"),
        ("pico-s-pro-standalone.update.txt", "update_body_sha256"),
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
        "profile", "firmware_version", "image_layout", "app_sources_sha256",
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
    verify()
