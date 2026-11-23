"""Stage CH340 operations through the existing CC3200 ROM reader and guarded writer."""

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import sys
import urllib.request

import pico_flash_backup_v2 as backup
import pico_flash_install as install
import pico_private_files as private
import pico_uart_probe as probe


MODELS = ["pico-s", "pico-pro", "pico-c"]
PUBLIC_BLOBS = {install.APP_NAME: "205f96477f9af5fd05e5506304d4e4b79e753c94",
                install.RECOVERY_NAME: "708a70b27c5a1171e7321360013e78071e1bfeb2"}


class CCError(ValueError):
    """Only fixed, user-facing messages are carried outside the private worker."""


@dataclass(frozen=True)
class Adapter:
    token: str
    label: str
    serial_port: str | None = None


def _token(kind, identifiers):
    identity = json.dumps(identifiers, separators=(",", ":")).encode()
    return "ch340-" + kind + "-" + hashlib.sha256(identity).hexdigest()[:32]


def _usb_id(dev):
    return _token("usb", [dev.bus, dev.address, list(dev.port_numbers or ())])


def _usb_devices():
    try:
        import usb.core
        import usb.backend.libusb1
    except ImportError:
        return []
    try:
        try:
            import libusb_package
            backend = libusb_package.get_libusb1_backend()
        except ImportError:
            backend = None
        backend = backend or usb.backend.libusb1.get_backend()
        if backend:
            return list(usb.core.find(backend=backend, find_all=True, idVendor=0x1a86, idProduct=0x7523))
    except (OSError, usb.core.USBError):
        pass
    return []


def _adapters():
    from serial.tools import list_ports
    result = {}
    for port in list_ports.comports():
        if (port.vid, port.pid) != (0x1a86, 0x7523):
            continue
        token = _token("serial", [port.device, port.serial_number, port.location])
        name = Path(port.device).name
        label = "Pico serial adapter"
        if re.fullmatch(r"[A-Za-z0-9._-]{1,80}", name):
            label += " · " + name
        result[token] = Adapter(token, label, port.device)
    # Standard Windows CH340 installs expose COM ports. The WinUSB path is
    # useful only for adapters already bound to WinUSB; avoid duplicate choices.
    if sys.platform == "win32" and result:
        return result
    for dev in _usb_devices():
        if dev.bus is None or dev.address is None:
            continue
        token = _usb_id(dev)
        result[token] = Adapter(token, f"Pico direct USB · bus {dev.bus}, device {dev.address}")
    return result


def devices():
    return [{"port": a.token, "label": a.label, "models": MODELS} for a in _adapters().values()]


def _transport(token):
    a = _adapters().get(token)
    if a is None:
        raise CCError("The selected CH340 has moved or disconnected. Refresh the USB devices.")
    if a.serial_port:
        return probe.SerialTransport(a.serial_port, 921600, exclusive=True)
    return probe.CH340USBTransport(match=lambda dev: _usb_id(dev) == token)


def _directory(path):
    private.ensure_private_directory(path, parents=True, exist_ok=True)
    private.assert_private(path, directory=True)
    return path


def _private_write(path, payload):
    """Publish without replacement, verifying a previously published copy."""
    temporary = path.with_name(".incoming-" + secrets.token_hex(16))
    with private.new_private_file(temporary) as handle:
        handle.write(payload)
        try:
            private.publish(handle, temporary, path)
        except FileExistsError:
            private.discard(handle, temporary)
    if private.read_regular(path, len(payload), private=True) != payload:
        raise CCError("A saved USB file failed verification. Its existing contents have been preserved.")
    if os.name == "posix":
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    return path


def _profile_name(model, display):
    if model == "pico-c" and display in ("ssd1309", "ssd1325"):
        return model + "-" + display
    if model in ("pico-s", "pico-pro") and not display:
        return model
    raise CCError("Choose the physical Pico model and the display controller for a Pico C.")


def _active_capture(image, cc, *, read_key):
    """Resolve the UID, application and optional key from independently valid FAT copies."""
    if len(image) != install.BACKUP_SIZE:
        raise CCError("This Pico has an unsupported flash size.")
    starts = identity = application = key = None
    for _, _, names, _ in install._filesystem_views(image, cc):
        current = install._slot_starts(names)
        if starts is not None and current != starts:
            raise CCError("The Pico's file tables disagree on its firmware or identity slots.")
        starts = current
        offset = starts[install.UID_NAME] * 4096
        identity = install._read_uid_record(image[offset:offset+41])
        _, application = install.inspection.read_candidate(image, names[install.APP_NAME], "")
        if read_key and install.KEY_NAME in names:
            _, value = install.inspection.read_candidate(image, names[install.KEY_NAME], "")
            if value is None or key is not None and key != value:
                raise CCError("The Pico's device key could not be verified. The capture is preserved.")
            key = install.validate_key(value)
    return identity, application, key


def _original(data, name, uid, captured, cc):
    base = _directory(data / "stock-firmware")
    group = _directory(base / name)
    directory = _directory(group / uid)
    # Both S and Pro labels have the same OEM application and boot geometry.
    # An older CLI capture can be reused only when it has this exact unit ID.
    directories = [directory]
    if name in ("pico-s", "pico-pro"):
        other = "pico-pro" if name == "pico-s" else "pico-s"
        sibling = base / other / uid
        if sibling.exists():
            private.assert_private(sibling.parent, directory=True)
            private.assert_private(sibling, directory=True)
            directories.append(sibling)
    for location in directories:
        for saved in sorted(location.glob("*.bin")):
            if not re.fullmatch(r"[0-9a-f]{64}", saved.stem):
                continue
            private.assert_private(saved)
            profile = install.inspect_stock(saved, name, cc)
            if profile.uid_digest.hex() != uid or profile.backup_sha256 != saved.stem:
                raise CCError("The saved original failed its model, identity or digest check.")
            return saved
    legacy = base / "original-serial-flash.bin"
    if uid == install.PROFILE_UID_SHA256 and name in ("pico-s", "pico-pro") and legacy.exists():
        private.assert_private(legacy)
        profile = install.inspect_stock(legacy, name, cc)
        if profile.uid_digest.hex() != uid or profile.backup_sha256 != install.BACKUP_SHA256:
            raise CCError("The historical stock capture failed its identity or digest check.")
        return legacy
    try:
        profile = install.inspect_stock(captured, name, cc)
    except install.InstallError:
        return None
    if profile.uid_digest.hex() != uid:
        raise CCError("The original firmware and the connected brewer have different identities.")
    return _private_write(directory / (profile.backup_sha256 + ".bin"),
                          private.read_regular(captured, install.BACKUP_SIZE, private=True))


def _public_slots(data, uid, model):
    if uid != install.PROFILE_UID_SHA256 or model == "pico-c":
        raise CCError("This brewer needs its own original stock capture for restoration. None is saved in this Hub.")
    directory = _directory(_directory(data / "stock-firmware") / "oem-slots")
    try:
        install.load_stock_directory(directory)
        return directory
    except install.InstallError:
        pass
    for name, blob in PUBLIC_BLOBS.items():
        metadata = install._expected_public(name)
        request = urllib.request.Request(
            "https://api.github.com/repos/jeffreydwalter/picobrew/git/blobs/" + blob,
            headers={"Accept": "application/vnd.github.raw+json", "User-Agent": "PicoBrew-Hub"})
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = response.read(metadata["size_bytes"] + 1)
        if install._image_metadata(payload) != metadata:
            raise CCError("GitHub returned a different image from the verified OEM slot.")
        _private_write(directory / Path(name).name, payload)
    install.load_stock_directory(directory)
    return directory


def run(action, model, port, display, data_dir, confirm, progress, *, job_key_path=None, expected_uid=None):
    name = _profile_name(model, display)
    if action not in ("backup", "install", "restore"):
        raise CCError("Choose a supported USB operation.")
    data = Path(data_dir)
    jobs = data / "usb-stock" / "cc3200"
    # The existing Hub data directory can use a deployment-specific mode.
    # Only private children store captures, keys or reports.
    _directory(data / "usb-stock")
    _directory(jobs)
    directory = _directory(jobs / ("session-" + secrets.token_hex(12)))
    factory = lambda: _transport(port)
    factory()  # identify the selected port before soliciting a reset
    _, cc, _, _ = backup.load_dependencies()
    capture = directory / "flash.bin"

    def report(line):
        if line.startswith("BREAK asserted."):
            progress("Power on the Pico or press and release its blue RESET button now. Keep USB connected.")
        elif re.fullmatch(r"READ [12]/2: [0-9,]+/[0-9,]+ bytes \([0-9]+%\)", line):
            progress(line)

    def failed(stage):
        if sys.platform == "linux":
            access = "Check the Hub account’s serial or USB device permissions and close any serial monitor."
        elif sys.platform == "win32":
            access = "For the standard CH340 driver choose its COM port and close any serial monitor."
        else:
            access = "Close serial monitors. Direct USB requires exclusive access to the CH340 on this Mac."
        raise CCError(stage + " failed. " + access + " Keep the private capture and report for diagnosis.")

    confirm("Connect only the intended idle Pico. Begin its verified full-flash backup?")
    progress("Preparing to read the current flash twice.")
    if backup.main(["--output", str(capture), "--power-wait", "120"], transport_factory=factory, progress=report):
        failed("The two-pass USB backup")
    image = private.read_regular(capture, install.BACKUP_SIZE, private=True)
    device_uid, application, captured_key = _active_capture(image, cc, read_key=action == "install")
    if expected_uid is not None and device_uid.decode("ascii") != expected_uid:
        raise CCError("The connected Pico has a different identity from the selected Hub machine.")
    uid = hashlib.sha256(device_uid).hexdigest()
    source = _original(data, name, uid, capture, cc)
    if action == "backup":
        progress("Both flash reads matched. The capture is saved in the Hub.")
        return
    target_args = ["--model", model] + (["--display", display] if display else [])
    if source is not None:
        source_args = ["--backup", str(source), "--stock-profile", *target_args]
    elif action == "restore":
        source_args = ["--oem-directory", str(_public_slots(data, uid, model)), "--model", model]
    else:
        raise CCError("Initial installation needs the original firmware for this specific Pico. Its current capture is preserved.")

    if action == "restore":
        confirm("Return this Pico to its stock firmware? Standalone Hub controls and wireless updates will become unavailable.")
        confirm("Write the verified original firmware slots and read them back?")
        stages = ["restore-stock"]
        key = None
    else:
        if install.main(["prepare-install", "--backup", str(source), "--stock-profile", *target_args], progress=report):
            raise CCError("The selected model, original backup or bundled application failed offline verification.")
        key_dir = _directory(data / "usb-stock" / "cc3200" / "keys")
        key = key_dir / (uid + ".key")
        if key.exists():
            key_bytes = install.load_key(key)
            if captured_key is not None and key_bytes != captured_key:
                raise CCError("The saved device key differs from the key on this Pico.")
        else:
            _private_write(key, captured_key or (secrets.token_hex(32) + "\n").encode())
        if application is None or install._image_metadata(application) not in install._approved_current_applications("install", name):
            raise CCError("This application is outside the supported set for the selected physical model.")
        stages = (["provision-key"] if captured_key is None else []) + ["install"]
        if model != "pico-c":
            stages.append("install-recovery")
    for stage in stages:
        if stage != "restore-stock":
            instruction = {"provision-key": "Set up and verify the brewer’s device key?",
                           "install": "Install and verify the standalone application and its website?",
                           "install-recovery": "Install and verify the separate wireless-update recovery image?"}
            confirm(instruction[stage])
        progress("Preparing the next USB session. Wait for the power-on or reset prompt.")
        args = [stage, *source_args, "--output", str(directory / (stage + ".json")), "--power-wait", "120"]
        if key is not None:
            args += ["--key-file", str(key), "--verify-backup-uid"]
        if stage == "install":
            firmware = (install.C_FIRMWARE / "standalone/pico-c" / display if display else install.RECOVERY_FILE.parent / model)
            args += ["--firmware", str(firmware / (model + "-standalone.bin"))]
        if install.main(args, transport_factory=factory, progress=report):
            failed("The " + stage + " stage")
    if job_key_path is not None and action == "install":
        _private_write(job_key_path.with_name("device-uid"), device_uid + b"\n")
        _private_write(job_key_path, install.load_key(key))
    progress("USB readback passed. Restart the Pico to boot the verified firmware.")
