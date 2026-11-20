#!/usr/bin/env python3
"""Create a private per-device bearer key for /local/api.key.

This command only creates a local file. It never connects to or flashes a Pico.
Provision the file through the verified UART connection after backing up the
device. Keep it outside the repository's tracked files and out of shared logs.
"""
import argparse
import os
from pathlib import Path
import secrets
import pico_private_files as private_files


def create_key(path):
    path = Path(path)
    private_files.ensure_private_directory(path.parent, parents=True, exist_ok=True)
    # Exclusive creation protects the only copy of a previously provisioned key.
    handle = private_files.new_private_file(path)
    try:
        with handle:
            handle.write((secrets.token_hex(32) + '\n').encode('ascii'))
            handle.flush()
            os.fsync(handle.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('private/api.key'),
                        help='New private file; existing files are never overwritten')
    args = parser.parse_args()
    try:
        path = create_key(args.output)
    except FileExistsError:
        parser.error('Key file already exists; choose another output path to rotate a key.')
    except OSError as exc:
        parser.error(str(exc))
    print('Created private API key: ' + str(path.resolve()))
    print('Device destination: /local/api.key (provisioning is a separate UART operation).')


if __name__ == '__main__':
    main()
