#!/usr/bin/env python3
"""Acquire and verify the selected W5500/DHCP/DNS rebuild dependency."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PIN = ROOT / 'dependencies/ethernet2.json'


def verify_source(directory):
    directory = Path(directory)
    pin = json.loads(PIN.read_text())
    for name, expected in pin['files'].items():
        data = (directory / name).read_bytes()
        if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
            raise RuntimeError('Pinned Ethernet2 identity mismatch: ' + name)


def acquire(destination):
    pin = json.loads(PIN.read_text())
    directory = Path(destination).resolve() / pin['directory']
    for name, expected in pin['files'].items():
        path = directory / name
        if not path.exists():
            with urllib.request.urlopen(pin['raw_base'] + name, timeout=60) as response:
                data = response.read(expected['bytes'] + 1)
            if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
                raise RuntimeError('Downloaded Ethernet2 identity mismatch: ' + name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    verify_source(directory)
    return directory


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, default=ROOT / 'build/dependencies')
    print(acquire(parser.parse_args().destination))
