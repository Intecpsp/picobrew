#!/usr/bin/env python3
"""Check the pinned recovery sources and, optionally, the rebuilt candidate."""

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true", help="Also check the rebuilt ARM package")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "import-provenance.json").read_text())
    for entry in manifest["source_files"]:
        path = ROOT / entry["path"]
        if not path.is_file() or sha256(path) != entry["sha256"]:
            raise SystemExit("Pinned recovery source differs: " + entry["path"])
    expected = manifest["candidate_status"]["binary_sha256"]
    packaged = ROOT / "firmware/recovery"
    if sha256(packaged / "pico-web-recovery.bin") != expected:
        raise SystemExit("Pinned recovery image differs from its source identity")
    if args.build:
        built = ROOT / "build/recovery"
        if sha256(built / "pico-web-recovery.bin") != expected:
            raise SystemExit("Rebuilt recovery image differs from the pinned package")
        report = json.loads((built / "recovery-report.json").read_text())
        original = json.loads((packaged / "recovery-report.json").read_text())
        if report["binary_sha256"] != expected or not report["image_layout"]["passed"]:
            raise SystemExit("Recovery build report failed binary/layout checks")
        if report["project_sources_sha256"] != original["project_sources_sha256"]:
            raise SystemExit("Recovery build source identities differ from the pinned package")
        if not report["sdk_link_passed"]:
            raise SystemExit("Recovery native SDK link checks failed")
        print("Recovery binary, source identities and native layout match the pinned package.")
    print(f"Verified {len(manifest['source_files'])} pinned recovery source files.")


if __name__ == "__main__":
    main()
