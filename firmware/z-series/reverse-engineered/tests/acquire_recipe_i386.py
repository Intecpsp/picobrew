#!/usr/bin/env python3
"""Acquire the optional pinned 32-bit JSON acceptance dependencies without root."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

PIN = Path(__file__).resolve().parent / "toolchains/recipe-i386.json"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def acquire(root):
    root = Path(root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    pin = json.loads(PIN.read_text())
    for package in pin["packages"] + [pin["qemu"]]:
        path = root / package["url"].rsplit("/", 1)[1]
        if not path.exists():
            with urllib.request.urlopen(package["url"], timeout=60) as response:
                data = response.read(package["bytes"] + 1)
            if len(data) != package["bytes"] or hashlib.sha256(data).hexdigest() != package["sha256"]:
                raise RuntimeError("Downloaded optional test package differs from its pin")
            path.write_bytes(data)
        if path.stat().st_size != package["bytes"] or digest(path) != package["sha256"]:
            raise RuntimeError("Cached optional test package differs from its pin")
        if package is pin["qemu"]:
            process = subprocess.Popen(["dpkg-deb", "--fsys-tarfile", str(path)], stdout=subprocess.PIPE)
            try:
                subprocess.run(["tar", "-x", "-C", str(root), package["member"]], stdin=process.stdout, check=True)
            finally:
                process.stdout.close()
            if process.wait():
                raise RuntimeError("Could not extract qemu-i386")
        else:
            subprocess.run(["dpkg-deb", "-x", str(path), str(root / "sysroot")], check=True)
    if digest(root / "usr/bin/qemu-i386") != pin["qemu"]["binary_sha256"]:
        raise RuntimeError("Extracted qemu-i386 differs from its pin")
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    print(acquire(parser.parse_args().destination))


if __name__ == "__main__":
    main()
