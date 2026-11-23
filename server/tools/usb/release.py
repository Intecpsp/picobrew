"""Assemble and verify the six host workers for a tagged public release."""

import hashlib
import json
from pathlib import Path
import re
import sys
import urllib.request
import zipfile

from build_worker import AVR, AVR_RELEASE, AVR_SOURCE, sha

SOURCES = {
    AVR_SOURCE[0]: (AVR_RELEASE + AVR_SOURCE[0], AVR_SOURCE[1]),
    "cc3200tool-c233289.zip": (
        "https://github.com/toniebox-reverse-engineering/cc3200tool/archive/c2332898a1ededc0bdd01ec279b23366d213f81e.zip",
        "17c7d1abb8cfb90bbf3189cd64c51ab4b06bca36547c51f1f96d4d551da6d097",
    ),
    "libusb-package-1.0.30.1.tar.gz": (
        "https://github.com/pyocd/libusb-package/archive/09711f4056efd55f5591ecfad1826c83b411c7c2.tar.gz",
        "4d3921a0c303f078b014ef73fc221a2a5a003375627dd298257a0bd7190e2760",
    ),
    "libusb-1.0.30.tar.gz": (
        "https://github.com/libusb/libusb/archive/87a55632db62c9bdc58cd31d3ccfa673f1bb017f.tar.gz",
        "26733c3eee2475c062e14f80a62ece4b33ee3b33fc0c88b2758964412a42b7e0",
    ),
    "esptool-4.11.0.tar.gz": (
        "https://files.pythonhosted.org/packages/22/74/fa0a4ff680d4b299135abc1ca06c0b8b58451adff9a0d4461ce62b03af96/esptool-4.11.0.tar.gz",
        "496571e4f6e36f7dc9a730dd485c4a9d522c9e7d6bb90ea2fec0a049275fbfad",
    ),
    "pyusb-1.3.1.tar.gz": (
        "https://files.pythonhosted.org/packages/00/6b/ce3727395e52b7b76dfcf0c665e37d223b680b9becc60710d4bc08b7b7cb/pyusb-1.3.1.tar.gz",
        "3af070b607467c1c164f49d5b0caabe8ac78dbed9298d703a8dbf9df4052d17e",
    ),
    "pyserial-3.5.tar.gz": (
        "https://files.pythonhosted.org/packages/1e/7d/ae3f0a63f41e4d2f6cb66a5b57197850f919f59e558159a4dd3a818f5082/pyserial-3.5.tar.gz",
        "3c77e014170dfffbd816e6ffc205e9842efb10be9f58ec16d3e8675b4925cddb",
    ),
    "pyinstaller-6.22.3.tar.gz": (
        "https://files.pythonhosted.org/packages/63/41/f90302845945abd4ed647933ff5ee7c6ac93983187be67f897b6cb613331/pyinstaller-6.22.3.tar.gz",
        "05eb2f5615503e72939a7224d68b4aff572c6b0438ee4a17d0a4b481f399362d",
    ),
}


def assemble(tag, directory):
    if not re.fullmatch(r"usb-tools-v\d+\.\d+\.\d+", tag):
        raise ValueError("Invalid USB release tag")
    assets = {}
    for key in sorted(AVR):
        asset = json.loads((directory / (key + ".json")).read_text())
        name = "picobrew-usb-" + key + (".exe" if key.startswith("windows") else "")
        worker = directory / name
        if (
            asset["platform"] != key
            or asset["name"] != name
            or asset["bytes"] != worker.stat().st_size
            or asset["sha256"] != sha(worker)
        ):
            raise ValueError("USB artifact does not match its provenance: " + key)
        assets[key] = asset
        hub = directory / name.replace("picobrew-usb-", "picobrew-hub-", 1)
        if not hub.is_file() or hub.stat().st_size < worker.stat().st_size:
            raise ValueError("Missing USB-enabled Hub for " + key)
        with zipfile.ZipFile(directory / f"usb-notices-{key}.zip") as notices:
            if notices.testzip() is not None or "versions.json" not in notices.namelist():
                raise ValueError("Invalid USB notices for " + key)
    (directory / "releases.json").write_text(json.dumps({"tag": tag, "assets": assets}, indent=2) + "\n")
    with zipfile.ZipFile(directory / "usb-programmer-sources.zip", "w") as archive:
        for name, (url, digest) in SOURCES.items():
            with urllib.request.urlopen(url, timeout=120) as response:
                data = response.read(100 * 1024 * 1024)
            if hashlib.sha256(data).hexdigest() != digest:
                raise ValueError("Programmer source digest mismatch: " + name)
            archive.writestr(name, data)
    checksum_file = directory / "SHA256SUMS.txt"
    products = sorted(path for path in directory.iterdir() if path.is_file() and path != checksum_file)
    checksum_file.write_text("".join(f"{sha(path)}  {path.name}\n" for path in products), encoding="ascii")


if __name__ == "__main__":
    assemble(sys.argv[1], Path(sys.argv[2]))
