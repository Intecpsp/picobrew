#!/usr/bin/env python3
"""Verify complete, byte-preserved firmware/source imports without network access."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess


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


def git_index_modes(root: Path) -> dict[str, str]:
    """Read executable-bit evidence that Windows filesystems cannot represent."""
    try:
        top = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "--show-toplevel"],
            stderr=subprocess.PIPE,
        ).decode("utf-8").strip()
        if Path(top).resolve() != root.resolve():
            raise ValueError("not the repository root")
        entries = subprocess.check_output(
            ["git", "-C", str(root), "ls-files", "--stage", "-z"],
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError, UnicodeError, ValueError):
        raise ValueError(
            "Windows import verification requires Git on PATH and a full Git "
            "checkout so preserved executable modes can be checked in its index."
        ) from None
    modes = {}
    for entry in entries.split(b"\0"):
        if not entry:
            continue
        metadata, name = entry.split(b"\t", 1)
        mode, _blob, stage = metadata.split()
        key = name.decode("utf-8")
        if stage != b"0":
            raise ValueError(f"Resolve unmerged Git index entry before verification: {key}")
        modes[key] = mode.decode("ascii")
    return modes


def verify_manifest(path: Path, *, index_modes: dict[str, str] | None = None) -> dict:
    if os.name == "nt" and index_modes is None:
        index_modes = git_index_modes(ROOT)
    manifest = json.loads(path.read_text(encoding="utf-8"))
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
        if index_modes is None:
            actual_mode = "100755" if target.stat().st_mode & stat.S_IXUSR else "100644"
        else:
            actual_mode = index_modes.get(destination.as_posix())
            if actual_mode is None:
                raise ValueError(f"Imported file is missing from the Git index: {destination}")
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
    return {"manifest": path.relative_to(ROOT).as_posix(), "files": len(destinations),
            "bytes": total, "source_tree": actual_tree, "verified": True,
            "mode_evidence": "git-index" if index_modes is not None else "filesystem"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", nargs="*", type=Path)
    args = parser.parse_args()
    manifests = args.manifests or [ROOT / "firmware/upstream-manifest.json",
                                 *sorted((ROOT / "firmware").glob("*/source-manifest.json"))]
    index_modes = git_index_modes(ROOT) if os.name == "nt" else None
    for path in manifests:
        print(json.dumps(verify_manifest(path.resolve(), index_modes=index_modes), sort_keys=True))
    for path in sorted((ROOT / "firmware").glob("*/provenance.json")):
        device = json.loads(path.read_text(encoding="utf-8")).get("oem_application")
        if not device:
            continue
        image = path.parent.joinpath(*safe_path(device["path"]).parts)
        data = image.read_bytes()
        if len(data) != device["size"] or hashlib.sha256(data).hexdigest() != device["sha256"]:
            raise ValueError(f"Verified device image changed: {image}")
        print(json.dumps({"device_image": image.relative_to(ROOT).as_posix(), "verified": True}))


if __name__ == "__main__":
    main()
