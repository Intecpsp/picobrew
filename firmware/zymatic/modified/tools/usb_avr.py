"""Zymatic USB service using the original updater's Arduino bootloader protocol."""

from contextlib import contextmanager
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import usb_stock as stock

FLASH = 128 * 1024
PAGE = 256
BOOT_MIN = 0x1e000
SIGNATURE = bytes.fromhex("1e9705")
OEM_PREFIX = "original-1.1.14.bin"


def select_usb_port(port, ports):
    found = [p for p in ports if p.device == port]
    if len(found) != 1 or (found[0].vid, found[0].pid) != (0x20a0, 0x421e):
        raise ValueError("Select the Zymatic USB port with PicoBrew VID:PID 20a0:421e")
    device = found[0]
    return (device.device, device.serial_number, device.location)


def usb_identity(port):
    try:
        from serial.tools import list_ports
    except ImportError as error:
        raise ValueError("Install pyserial==3.5 to identify the Zymatic USB port") from error
    return select_usb_port(port, list_ports.comports())


def programmer():
    path = shutil.which("avrdude")
    if not path:
        raise ValueError("Install AVRDUDE 8.1 and put avrdude on PATH, or use PicoFlash")
    result = subprocess.run([path, "--version"], capture_output=True, text=True, timeout=10)
    # Windows PATHEXT may preserve the uppercase .EXE spelling in argv[0].
    accepted = {"avrdude version 8.1", "avrdude.exe version 8.1"}
    if result.returncode or accepted.isdisjoint(line.strip().lower() for line in (result.stdout + result.stderr).splitlines()):
        raise ValueError("Use the tested AVRDUDE version 8.1, or prepare the HEX for PicoFlash")
    return os.path.abspath(path)


class Controller:
    def __init__(self, port):
        self.port = port
        self.identity = usb_identity(port)
        self.executable = programmer()

    def run(self, work, operations, *, read_only):
        if usb_identity(self.port) != self.identity:
            raise ValueError("Zymatic USB identity changed; select the connected brewer again")
        if not read_only and not any(self.identity[1:]):
            raise ValueError("A USB location or serial number is required before writing to the Zymatic")
        command = [self.executable, "-N", "-c", "arduino", "-p", "m1284p",
                   "-P", self.port, "-b", "115200", "-D", "-A"]
        if read_only:
            command.append("-n")
        for operation in operations:
            command.extend(["-U", operation])
        # A single process owns the serial connection from baseline verification
        # through the write; a failed verify or signature check aborts the write.
        try:
            result = subprocess.run(command, cwd=work, stdin=subprocess.DEVNULL, timeout=480)
        except subprocess.TimeoutExpired as exc:
            raise ValueError("Timed out communicating with the Zymatic; inspect AVRDUDE output") from exc
        if result.returncode:
            raise ValueError("AVRDUDE failed. Check the serial connection and the output above")


def write_input(path, data):
    with stock.private_files.new_private_file(path) as file:
        file.write(data)


def read_flash(controller, work, index):
    sig_name, flash_name = f"signature-{index}.bin", f"read-{index}.bin"
    write_input(work / sig_name, b"")
    write_input(work / flash_name, b"")
    controller.run(work, [f"signature:r:{sig_name}:r", f"flash:r:{flash_name}:r"], read_only=True)
    signature = stock.private_files.read_regular(work / sig_name, 3, private=True)
    if signature != SIGNATURE:
        raise ValueError("The controller signature differs from the Zymatic ATmega1284P")
    image = stock.private_files.read_regular(work / flash_name, FLASH, private=True)
    app = image[:stock.APP_SIZE]
    whole_pages = len(app) // PAGE
    if len(image) != FLASH or app[:PAGE] * whole_pages == app[:whole_pages * PAGE]:
        raise ValueError("Flash read is clipped, blank or repeating; check read protection")
    return image


def plan(before, application):
    if len(before) != FLASH or not stock.is_original_app(application):
        raise ValueError("Restoration requires a full live read and the verified stock application")
    end = ((len(application) + PAGE - 1) // PAGE) * PAGE
    if end > BOOT_MIN:
        raise ValueError("Application pages overlap the possible bootloader region")
    return application + before[len(application):end]


class ServiceBusy(ValueError):
    """Another process still owns the USB port protocol."""


@contextmanager
def service_lock(directory):
    # POSIX/Windows release the lock on process exit. Keep the inode to avoid
    # split locks when two Hub service commands are started at once.
    path = directory / "usb-service.lock"
    try:
        write_input(path, b"0")
    except FileExistsError:
        stock.private_files.assert_private(path)
    with path.open("r+b") as file:
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise ServiceBusy("Another USB service is active for this Hub") from exc
        yield


def service(data, port, action, source=None, *, confirm=None, key_file=None, hub_host=None, offline_program=None, expected_uid=None):
    if action not in ("backup", "install", "restore"):
        raise ValueError("Unknown Zymatic USB service action")
    directory = Path(data).absolute() / "usb-stock" / "zymatic"
    stock.private_files.ensure_private_directory(directory, parents=True, exist_ok=True)
    stock.private_files.assert_private(directory, directory=True)
    with service_lock(directory), tempfile.TemporaryDirectory(dir=directory) as temporary:
        work = Path(temporary)
        supplied = None
        if source is not None:
            supplied = stock.decode_application(stock.private_files.read_regular(
                stock.prepare(data, source), stock.MAX_HEX, private=True))
        controller = Controller(port)
        print("Reading the idle Zymatic twice. Keep power and USB connected.")
        before = read_flash(controller, work, 1)
        if read_flash(controller, work, 2) != before:
            raise ValueError("Independent flash reads differ; no valid capture")
        digest = hashlib.sha256(before).hexdigest()
        capture = stock.save_private(directory / f"avr-flash-{digest}.bin", before)
        print(f"Saved two matching 128 KiB AVR flash reads to {capture}")
        prefix = before[:stock.APP_SIZE]
        if action == "backup":
            if stock.is_original_app(prefix):
                stock.save_private(directory / OEM_PREFIX, prefix)
                print("The installed application matches the original Zymatic 1.1.14.")
            else:
                print("The capture is retained. Its application is unrecognized as stock 1.1.14.")
            return

        import usb_application
        installed = usb_application.build.recognize_private_application(before)
        identity = getattr(controller, "identity", None)
        previous = usb_application.latest_transaction(directory, usb_application.transaction_prefix(identity))
        pending = previous and not usb_application.verified_transaction(previous)
        stock_prefix = stock.is_original_app(prefix)
        if action == "install" and not installed and not stock_prefix and not previous:
            raise ValueError("Initial conversion requires verified Zymatic stock 1.1.14")
        if stock.is_original_app(prefix):
            stock.save_private(directory / OEM_PREFIX, prefix)
        application = supplied
        if application is None:
            try:
                application = stock.private_files.read_regular(directory / OEM_PREFIX,
                                                              stock.APP_SIZE, private=True)
            except FileNotFoundError:
                application = stock.decode_application(stock.private_files.read_regular(
                    stock.prepare(data), stock.MAX_HEX, private=True))
        if not stock.is_original_app(application):
            raise ValueError("Restoration requires the verified stock application")
        # Recover the paired identity even if a previous attempt stopped in
        # its configuration/recipe page. Model the next write from the actual
        # double-read, never by replaying a saved full-flash dump.
        standalone = before if installed else None
        unknown = not installed and (not stock_prefix or before[stock.APP_SIZE:BOOT_MIN].strip(b"\xff"))
        if previous and (pending or unknown):
            interrupted = usb_application.interrupted_standalone(previous, before, application, pending=bool(pending))
            if interrupted:
                standalone, installed = interrupted
        if expected_uid and (not installed or installed["uid"] != expected_uid):
            raise ValueError("The USB Zymatic differs from the registered brewer; no write was attempted")
        if action == "install":
            pages, metadata = usb_application.install_pages(standalone or before, application, installed,
                                                           key_file, hub_host, offline_program)
            version = metadata["firmware_version"]
            confirmations = ("INSTALL ZYMATIC", "WRITE " + version)
        else:
            pages = plan(before, application)
            if installed:
                pages = application.ljust(usb_application.end_page(installed["application_bytes"]), b"\xff")
            version = "1.1.14"
            confirmations = ("RESTORE ZYMATIC", "WRITE 1.1.14")
        if action == "restore" and not installed and before[stock.APP_SIZE:BOOT_MIN].strip(b"\xff"):
            print("Unidentified bytes follow the stock application. This restore preserves that data.")
        if pages == before[:len(pages)]:
            if pending:
                usb_application.finish_transaction(previous, before)
            print(f"The installed application already matches {version}. No write needed.")
            return metadata["uid"] if action == "install" else None
        write_input(work / "app-pages.bin", pages)
        expected = pages + before[len(pages):]
        print(f"Write Zymatic {version} via {port}. This updates the first {len(pages) // PAGE} "
              "application pages. Keep power and USB connected until readback finishes.")
        if confirm is None:
            if not sys.stdin.isatty():
                raise ValueError("Use a local interactive terminal to confirm the operation")
            confirm = input
        for phrase in confirmations:
            if confirm("Type " + phrase + ": ") != phrase:
                raise ValueError("USB operation canceled")
        transaction = None
        if action == "install" or installed:
            transaction = usb_application.remember_transaction(
                directory, identity, before, expected, expected if action == "install" else standalone, len(pages))
        controller.run(work, ["flash:v:read-1.bin:r", "flash:w:app-pages.bin:r"], read_only=False)
        if read_flash(controller, work, 3) != expected:
            raise ValueError("Final flash readback differs; installation is unverified")
        if transaction:
            usb_application.finish_transaction(transaction, expected)
        print(f"Verified the entire readable program flash for Zymatic {version}. Disconnect USB "
              "and restart the brewer.")
        return metadata["uid"] if action == "install" else None
