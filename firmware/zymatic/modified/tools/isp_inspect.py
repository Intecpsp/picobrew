"""Read Zymatic boot configuration with an external Atmel-ICE on a verified ISP header."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

import boot_evidence
import usb_avr
import usb_stock as stock


MEMORIES = {"signature": 3, "lfuse": 1, "hfuse": 1, "efuse": 1, "lock": 1,
            "flash": usb_avr.FLASH, "eeprom": 4096}


def read_once(executable, serial, directory):
    # Use the external ISP transport. The rear USB bootloader cannot be used
    # to infer real fuse bytes from STK500 universal-command acknowledgements.
    command = [executable, "-N", "-p", "m1284p", "-c", "atmelice_isp", "-P", "usb:" + serial,
               "-B", "10", "-n", "-D", "-A"]
    for memory in MEMORIES:
        filename = memory + ".bin"
        usb_avr.write_input(directory / filename, b"")
        command.extend(["-U", f"{memory}:r:{filename}:r"])
    subprocess.run(command, cwd=directory, stdin=subprocess.DEVNULL, check=True, timeout=480)
    result = {}
    for memory, size in MEMORIES.items():
        value = stock.private_files.read_regular(directory / (memory + ".bin"), size, private=True)
        if len(value) != size:
            raise ValueError(f"External ISP returned an incomplete {memory} read")
        result[memory] = value
    return result


def inspect(data, serial, serial_backup):
    if not re.fullmatch(r"[A-Za-z0-9_-]{4,64}", serial):
        raise ValueError("Enter the serial number printed on the external Atmel-ICE programmer")
    backup = stock.private_files.read_regular(serial_backup, usb_avr.FLASH, private=True)
    if len(backup) != usb_avr.FLASH:
        raise ValueError("Select the complete private rear-USB flash backup from the same brewer")
    if not stock.is_original_app(backup[:stock.APP_SIZE]):
        import usb_layout
        if not usb_layout.identify(backup):
            raise ValueError("Select a rear-USB capture containing a verified Zymatic application")
    directory = Path(data).absolute() / "usb-stock" / "zymatic"
    stock.private_files.ensure_private_directory(directory, parents=True, exist_ok=True)
    stock.private_files.assert_private(directory, directory=True)
    executable = usb_avr.programmer()
    with usb_avr.service_lock(directory):
        with tempfile.TemporaryDirectory(dir=directory) as work:
            first = read_once(executable, serial, Path(work))
        if first["signature"] != usb_avr.SIGNATURE:
            raise ValueError("External ISP signature differs from the Zymatic ATmega1284P")
        with tempfile.TemporaryDirectory(dir=directory) as work:
            if read_once(executable, serial, Path(work)) != first:
                raise ValueError("External ISP reads differ; no valid capture")
        flash, lock, hfuse = first["flash"], first["lock"][0], first["hfuse"][0]
        if (lock & 3) not in (2, 3):
            raise ValueError("External flash verification is disabled or the lock mode is reserved")
        if flash[:usb_avr.BOOT_MIN] != backup[:usb_avr.BOOT_MIN]:
            raise ValueError("External ISP application region differs from the rear-USB backup")
        boot = boot_evidence.describe(flash)
        # Table 27-7 expresses the start and length in words. Use byte
        # ranges consistently with the serial flash fingerprints.
        boot_region = boot["possible_boot_regions"][(hfuse >> 1) & 3]
        report = {
            "schema_version": 1, "interface": "atmelice_isp",
            "signature": first["signature"].hex(), "usb_backup_sha256": sha256(backup).hexdigest(),
            "flash_sha256": boot["flash_sha256"], "eeprom_sha256": sha256(first["eeprom"]).hexdigest(),
            "registers": {name: first[name].hex() for name in ("lfuse", "hfuse", "efuse", "lock")},
            "configured_boot_region": boot_region,
            "reset_destination": "application" if hfuse & 1 else "boot-section",
        }
        for name in ("flash", "eeprom"):
            stock.save_private(directory / f"isp-{name}-{report[name + '_sha256']}.bin", first[name])
        raw = (json.dumps(report, indent=2) + "\n").encode("ascii")
        return stock.save_private(directory / f"isp-evidence-v1-{sha256(raw).hexdigest()}.json", raw)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True, help="Private Hub data directory")
    parser.add_argument("--usb-backup", type=Path, required=True, help="Rear-USB full-flash backup of this unit")
    parser.add_argument("--programmer-serial", required=True, help="Serial printed on the external Atmel-ICE")
    args = parser.parse_args()
    try:
        path = inspect(args.data, args.programmer_serial, args.usb_backup)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")
    print(f"Verified two external ISP reads and saved the private configuration report to {path}")
