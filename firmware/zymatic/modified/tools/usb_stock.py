"""Prepare and cache the original Zymatic 1.1.14 application for PicoFlash."""

import argparse
import hashlib
import io
from pathlib import Path
import sys
import uuid
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
import private_files

FILENAME = "Zymatic_1_1_14.hex"
SHA256 = "eb04c9dafdae032e7770b142f868246caf82eed4d80a0989552fdfd5dfee7b7c"
VENDOR = "https://picobrew.com/Members/Software/Zymatic/Installation.cshtml"
MAX_HEX = 2 * 1024 * 1024
MAX_ZIP = 8 * 1024 * 1024


def verify(contents):
    # The reference hash covers the entire Intel HEX text including its line endings.
    if hashlib.sha256(contents).hexdigest() != SHA256:
        raise ValueError("Expected the original Zymatic 1.1.14 HEX (SHA-256 " + SHA256 + ")")


def read_source(path):
    raw = private_files.read_regular(path, MAX_ZIP)
    if raw.startswith(b":"):
        if len(raw) > MAX_HEX:
            raise ValueError("Zymatic HEX file exceeds the size limit")
        contents = raw
    else:
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entries = archive.infolist()
            members = [x for x in entries if not x.is_dir() and x.filename.lower().endswith(".hex")]
            if len(members) != 1 or len(entries) > 128:
                raise ValueError("Select one vendor HEX file or its ZIP with one firmware HEX")
            entry = members[0]
            if entry.file_size > MAX_HEX or entry.flag_bits & 1:
                raise ValueError("Encrypted or oversized HEX in Zymatic ZIP")
            if entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                raise ValueError("Use the original vendor ZIP or its extracted HEX file")
            with archive.open(entry) as file:
                contents = file.read(MAX_HEX + 1)
            if len(contents) != entry.file_size:
                raise ValueError("Invalid Zymatic HEX length")
    verify(contents)
    return contents


def prepare(data, source=None):
    directory = Path(data).absolute() / "usb-stock" / "zymatic"
    destination = directory / FILENAME
    contents = read_source(source) if source else None
    if source:
        private_files.ensure_private_directory(directory, parents=True, exist_ok=True)
    try:
        private_files.assert_private(directory, directory=True)
        cached = private_files.read_regular(destination, MAX_HEX, private=True)
    except FileNotFoundError:
        if contents is None:
            raise ValueError("No saved Zymatic 1.1.14 HEX. Obtain it with PicoFlash from "
                             + VENDOR + " and pass --oem-file FILE.")
    else:
        verify(cached)
        return destination

    partial = directory / (uuid.uuid4().hex + ".partial")
    with private_files.new_private_file(partial) as file:
        try:
            file.write(contents)
            private_files.publish(file, partial, destination)
        except FileExistsError:
            private_files.discard(file, partial)
        except BaseException:
            private_files.discard(file, partial)
            raise
    verify(private_files.read_regular(destination, MAX_HEX, private=True))
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path, help="Hub data directory")
    parser.add_argument("--oem-file", type=Path, help="original vendor ZIP or unmodified HEX")
    args = parser.parse_args()
    try:
        destination = prepare(args.data, args.oem_file)
    except (ValueError, OSError, zipfile.BadZipFile, EOFError, zlib.error) as error:
        parser.exit(1, f"{error}\n")
    print(f"Verified Zymatic stock application 1.1.14:\n{destination}")
    print("On a Windows computer with PicoFlash, connect the idle Zymatic by USB, "
          "power it on, select this HEX with [...], then choose Flash Now. "
          "After restart, confirm 1.1.14 in About Zymatic.")


if __name__ == "__main__":
    main()
