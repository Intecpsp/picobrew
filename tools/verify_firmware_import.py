#!/usr/bin/env python3
"""Verify complete, byte-preserved firmware/source imports without network access."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat


ROOT = Path(__file__).resolve().parents[1]


def git_hash(kind: str, data: bytes) -> str:
    return hashlib.sha1(kind.encode() + b" " + str(len(data)).encode() + b"\0" + data).hexdigest()


def safe_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(part in ("..", ".git") for part in path.parts):
        raise ValueError(f"Invalid manifest path: {value}")
    if path.as_posix() != value:
        raise ValueError(f"Noncanonical manifest path: {value}")
    return path


def tree_hash(node: dict) -> str:
    entries = []
    for name, value in node.items():
        if isinstance(value, dict):
            mode, digest, key = "40000", tree_hash(value), name.encode() + b"/"
        else:
            mode, digest = value
            key = name.encode()
        entry = mode.encode() + b" " + name.encode() + b"\0" + bytes.fromhex(digest)
        entries.append((key, entry))
    return git_hash("tree", b"".join(entry for _, entry in sorted(entries)))


def verify_manifest(path: Path) -> dict:
    manifest = json.loads(path.read_text())
    prefix = manifest["source_root"].strip("/")
    prefix = prefix + "/" if prefix else ""
    tree = {}
    destinations = set()
    total = 0
    for item in manifest["files"]:
        source = item["source_path"]
        if not source.startswith(prefix):
            raise ValueError(f"Source outside declared subtree: {source}")
        source_path = safe_path(source[len(prefix):])
        destination = safe_path(item["destination_path"])
        if destination in destinations:
            raise ValueError(f"Repeated destination: {destination}")
        destinations.add(destination)
        target = ROOT.joinpath(*destination.parts)
        if target.is_symlink() or not target.is_file() or not target.resolve().is_relative_to(ROOT):
            raise ValueError(f"Expected regular in-repository file: {destination}")
        data = target.read_bytes()
        actual_mode = "100755" if target.stat().st_mode & stat.S_IXUSR else "100644"
        expected = (item["size"], item["sha256"], item["git_blob_sha"], item["mode"])
        actual = (len(data), hashlib.sha256(data).hexdigest(), git_hash("blob", data), actual_mode)
        if actual != expected:
            raise ValueError(f"Imported bytes or mode changed: {destination}")
        node = tree
        for part in source_path.parts[:-1]:
            node = node.setdefault(part, {})
            if not isinstance(node, dict):
                raise ValueError(f"Conflicting source path: {source}")
        if source_path.name in node:
            raise ValueError(f"Repeated source path: {source}")
        node[source_path.name] = (item["mode"], item["git_blob_sha"])
        total += len(data)
    actual_tree = tree_hash(tree)
    if actual_tree != manifest["source_tree"]:
        raise ValueError(f"Incomplete or changed source tree in {path}: {actual_tree}")
    return {"manifest": str(path.relative_to(ROOT)), "files": len(destinations),
            "bytes": total, "source_tree": actual_tree, "verified": True}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", nargs="*", type=Path)
    args = parser.parse_args()
    manifests = args.manifests or [ROOT / "firmware/upstream-manifest.json",
                                 *sorted((ROOT / "firmware").glob("*/source-manifest.json"))]
    for path in manifests:
        print(json.dumps(verify_manifest(path.resolve()), sort_keys=True))
    for path in sorted((ROOT / "firmware").glob("*/provenance.json")):
        device = json.loads(path.read_text()).get("oem_application")
        if not device:
            continue
        image = path.parent.joinpath(*safe_path(device["path"]).parts)
        data = image.read_bytes()
        if len(data) != device["size"] or hashlib.sha256(data).hexdigest() != device["sha256"]:
            raise ValueError(f"Verified device image changed: {image}")
        print(json.dumps({"device_image": str(image.relative_to(ROOT)), "verified": True}))


if __name__ == "__main__":
    main()
