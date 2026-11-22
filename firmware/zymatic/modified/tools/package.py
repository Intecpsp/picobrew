"""Build a private Zymatic image from its pinned factory application and extension."""

import argparse
import json
from pathlib import Path
import secrets

import build
import offline_program
from usb_application import read_key
import usb_stock as stock


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path("private/build"))
    parser.add_argument("--oem-file", type=Path)
    parser.add_argument("--key-file", type=Path)
    parser.add_argument("--uid")
    parser.add_argument("--hub-host", default="picobrew.com")
    parser.add_argument("--offline-program", type=Path)
    args = parser.parse_args()
    recipe = offline_program.read_program(args.offline_program)
    if bool(args.key_file) != bool(args.uid):
        parser.error("To use an existing brewer identity, provide both --key-file and --uid")
    directory = args.data.absolute()
    stock.private_files.ensure_private_directory(directory, parents=True, exist_ok=True)
    stock.private_files.assert_private(directory, directory=True)
    source = stock.prepare(directory, args.oem_file)
    application = stock.decode_application(stock.private_files.read_regular(source, stock.MAX_HEX, private=True))
    key = read_key(args.key_file or directory / "api.key", create=not args.key_file)
    uid = args.uid
    if not uid:
        uid_file = directory / "device.uid"
        try:
            uid = stock.private_files.read_regular(uid_file, 32, private=True).decode()
        except FileNotFoundError:
            uid = secrets.token_hex(16)
            stock.save_private(uid_file, uid.encode())
    image, report = build.build_application(application, key, uid, args.hub_host, recipe)
    target = directory / report["binary_sha256"]
    stock.private_files.ensure_private_directory(target, exist_ok=True)
    for name, contents in [(f"zymatic-{build.VERSION}.bin", image),
                           (f"zymatic-{build.VERSION}.hex", build.intel_hex(image)),
                           ("manifest.json", (json.dumps(report, indent=2) + "\n").encode())]:
        stock.save_private(target / name, contents)
    print(f"Built private Zymatic {build.VERSION} application ({len(image)} bytes) in {target}")


if __name__ == "__main__":
    main()
