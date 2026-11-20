#!/usr/bin/env python3
"""Fetch the explicitly selected, hash-pinned JSON rebuild dependency."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

COMPONENT = Path(__file__).resolve().parents[1]
PIN = COMPONENT / "dependencies/arduinojson.json"


def verify_source(directory):
    directory = Path(directory)
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    entries = [f"{path.relative_to(directory).as_posix()} {hashlib.sha256(path.read_bytes()).hexdigest()}\n"
               for path in sorted((directory / "src").rglob("*")) if path.is_file()]
    if (len(entries) != pin["source_file_count"] or
            hashlib.sha256("".join(entries).encode()).hexdigest() != pin["source_tree_sha256"]):
        raise RuntimeError("ArduinoJson source differs from the pinned official archive")


def acquire(destination):
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / (pin["directory"] + ".tar.gz")
    if not archive.exists():
        with urllib.request.urlopen(pin["url"], timeout=60) as response:
            data = response.read(pin["archive_bytes"] + 1)
        if (len(data) != pin["archive_bytes"] or
                hashlib.sha256(data).hexdigest() != pin["archive_sha256"]):
            raise RuntimeError("Downloaded ArduinoJson archive differs from the pinned bytes")
        archive.write_bytes(data)
    data = archive.read_bytes()
    if (len(data) != pin["archive_bytes"] or
            hashlib.sha256(data).hexdigest() != pin["archive_sha256"]):
        raise RuntimeError("Cached ArduinoJson archive differs from the pinned bytes")
    with tarfile.open(archive, "r:gz") as source:
        for member in source.getmembers():
            path = Path(member.name)
            if (path.is_absolute() or ".." in path.parts or
                    not path.parts or path.parts[0] != pin["directory"] or
                    not (member.isfile() or member.isdir())):
                raise RuntimeError("Unsafe archive member in pinned ArduinoJson archive")
        source.extractall(destination, filter="data")
    directory = destination / pin["directory"]
    verify_source(directory)
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path,
                        default=COMPONENT / "build/dependencies")
    options = parser.parse_args()
    print(acquire(options.destination))


if __name__ == "__main__":
    main()
