"""Prepare, back up or restore Zymatic application firmware."""

import argparse
import hashlib
import io
from pathlib import Path
import sys
import urllib.request
import uuid
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
import private_files

FILENAME = "Zymatic_1_1_14.hex"
SHA256 = "eb04c9dafdae032e7770b142f868246caf82eed4d80a0989552fdfd5dfee7b7c"
ARCHIVE_URL = ("https://raw.githubusercontent.com/chiefwigms/picobrew_pico/"
               "36acae3003845b72568ef5f7d6cf168acb6f9b69/app/static/support/legacy/"
               "zymatic/Zymatic_1_1_14.zip")
ARCHIVE_SIZE = 107312
ARCHIVE_SHA256 = "961db98db6b73a1c91c5eea7495db2b0343ff76dc9af142f23cc921d008e2d1f"
APP_SIZE = 113610
APP_SHA256 = "d6bff3e9c49c092a9ed825b91790c0adbc46be7112f3d79f26d9a30d28e0a4cd"
MAX_HEX = 2 * 1024 * 1024
MAX_ZIP = 8 * 1024 * 1024


def verify(contents):
    # The reference hash covers the entire Intel HEX text including its line endings.
    if hashlib.sha256(contents).hexdigest() != SHA256:
        raise ValueError("Expected the original Zymatic 1.1.14 HEX (SHA-256 " + SHA256 + ")")


def unpack_source(raw):
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


def fetch_source():
    with urllib.request.urlopen(ARCHIVE_URL, timeout=20) as response:
        raw = response.read(ARCHIVE_SIZE + 1)
    if len(raw) != ARCHIVE_SIZE or hashlib.sha256(raw).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("The downloaded Zymatic archive differs from the pinned original")
    return unpack_source(raw)


def decode_application(contents):
    verify(contents)
    result = bytearray()
    segment = 0
    ended = False
    for line in contents.splitlines():
        if ended or not line.startswith(b":"):
            raise ValueError("Invalid Zymatic Intel HEX record")
        row = bytes.fromhex(line[1:].decode("ascii"))
        if len(row) < 5 or len(row) != row[0] + 5 or sum(row) & 255:
            raise ValueError("Invalid Zymatic Intel HEX checksum or length")
        address = int.from_bytes(row[1:3], "big")
        kind, data = row[3], row[4:-1]
        if kind == 0 and segment + address == len(result):
            result.extend(data)
        elif kind == 2 and address == 0 and len(data) == 2:
            segment = int.from_bytes(data, "big") << 4
        elif kind == 1 and address == 0 and not data:
            ended = True
        else:
            raise ValueError("Unexpected Zymatic Intel HEX address or record type")
    if not ended or not is_original_app(result):
        raise ValueError("Zymatic application address range or SHA-256 mismatch")
    return bytes(result)


def is_original_app(data):
    return len(data) == APP_SIZE and hashlib.sha256(data).hexdigest() == APP_SHA256


def save_private(destination, contents):
    partial = destination.parent / (uuid.uuid4().hex + ".partial")
    with private_files.new_private_file(partial) as file:
        try:
            file.write(contents)
            private_files.publish(file, partial, destination)
        except FileExistsError:
            private_files.discard(file, partial)
        except BaseException:
            private_files.discard(file, partial)
            raise
    if private_files.read_regular(destination, len(contents), private=True) != contents:
        raise ValueError(f"The saved file differs from the verified bytes: {destination}")
    return destination


def prepare(data, source=None):
    directory = Path(data).absolute() / "usb-stock" / "zymatic"
    destination = directory / FILENAME
    contents = unpack_source(private_files.read_regular(source, MAX_ZIP)) if source else None
    try:
        private_files.assert_private(directory, directory=True)
        cached = private_files.read_regular(destination, MAX_HEX, private=True)
    except FileNotFoundError:
        pass
    else:
        verify(cached)
        return destination

    if contents is None:
        contents = fetch_source()
    private_files.ensure_private_directory(directory, parents=True, exist_ok=True)
    private_files.assert_private(directory, directory=True)
    return save_private(destination, contents)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path, help="Hub data directory")
    parser.add_argument("--oem-file", type=Path, help="original vendor ZIP or unmodified HEX")
    parser.add_argument("--action", choices=("prepare", "backup", "restore", "install"), default="prepare")
    parser.add_argument("--port", help="local Zymatic USB serial port")
    parser.add_argument("--key-file", type=Path, help="Private per-brewer API key for standalone setup")
    parser.add_argument("--hub-host", help="Recipe server hostname or IPv4 address with optional port")
    parser.add_argument("--offline-program", type=Path, help="Compiled Zymatic recipe JSON for offline brewing")
    args = parser.parse_args()
    if bool(args.port) != (args.action != "prepare"):
        parser.error("--port is required for USB backup, restore or install")
    if args.oem_file and args.action == "backup":
        parser.error("backup reads the Zymatic directly and takes no --oem-file")
    if args.action == "install" and (not args.key_file or not args.key_file.is_absolute()):
        parser.error("Standalone setup requires an absolute --key-file")
    if args.action != "install" and (args.key_file or args.hub_host or args.offline_program):
        parser.error("--key-file, --hub-host and --offline-program apply to standalone installation")
    try:
        if args.action != "prepare":
            import offline_program
            import usb_avr
            usb_avr.service(args.data, args.port, args.action, args.oem_file,
                            key_file=args.key_file, hub_host=args.hub_host,
                            offline_program=offline_program.read_program(args.offline_program))
            return
        destination = prepare(args.data, args.oem_file)
    except (ValueError, OSError, zipfile.BadZipFile, EOFError, zlib.error) as error:
        parser.exit(1, f"{error}\n")
    print(f"Verified Zymatic stock application 1.1.14:\n{destination}")
    print("On a Windows computer with PicoFlash, connect the idle Zymatic by USB, "
          "power it on, select this HEX with [...], then choose Flash Now. "
          "After restart, confirm 1.1.14 in About Zymatic.")


if __name__ == "__main__":
    main()
