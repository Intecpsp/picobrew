#!/usr/bin/env python3
"""Independent wire-level mutation tests for the constrained Pico installer.

All firmware, keys, filesystem contents, and raw backups are synthetic. The
real cc3200tool packet framing is used against an independent fake device.
No USB device is opened. Existing-write opens erase the fake file immediately,
matching the destructive behavior that the installer must journal and guard.
"""
import contextlib
import hashlib
import io
import json
import logging
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

from cc3200tool import cc
import pico_uart_probe as probe
import pico_flash_backup_v2 as backup
import pico_flash_preflight as preflight
import pico_flash_install as installer
from pico_backup_fake_device import FakePicoUSB
from test_pico_flash_preflight import fixture_images, NORMAL_BOOTINFO, PassthroughReceive
from test_pico_inspect_backup import add_fat

HELPER = cc.dll_data("rbtl3100s.dll")
ACK = b"\x00\xcc"
KEY_NAME = "/local/api.key"
APP_NAME = "/sys/mcuimg2.bin"
KEY = b"c94e17f826b305da" * 4 + b"\n"
OTHER_KEY = b"7a28c15ef0396db4" * 4 + b"\n"
CLOSE_PACKET = b"\x22" + b"\x00" * 63 + b"\x46" * 256 + b"\x00"
PROTECTED = ("/sys/mcuimg.bin", "/sys/mcuimg1.bin", "/sys/mcubootinfo.bin")
ALLOWED_NAMES = frozenset((*PROTECTED, APP_NAME, "/sys/mcuimg3.bin", KEY_NAME))


def candidate_image(length):
    return struct.pack("<II", 0x20036000, 0x20018CD1) + bytes(
        (i * 23 + i // 251) & 255 for i in range(length - 8)
    )


def backup_fixture(files):
    """Construct the observed allocation locations, using only synthetic bytes."""
    image = bytearray(b"\xff" * (1024 * 1024))
    entries = [
        (0, "/sys/mcuimg.bin", 71, 4, True),
        (9, "/sys/mcubootinfo.bin", 79, 1, True),
        (10, "/sys/mcuimg1.bin", 214, 16, False),
        (18, "/sys/mcuimg2.bin", 130, 48, False),
    ]
    if KEY_NAME in files:
        entries.append((20, KEY_NAME, 81, 1, False))
    if "/sys/mcuimg3.bin" in files:
        entries.append((21, "/sys/mcuimg3.bin", 82, 1, False))
    add_fat(image, 1, 225, entries)
    for _, name, block, _, _ in entries:
        data = files[name]
        offset = block * 4096
        image[offset:offset + 8] = len(data).to_bytes(3, "little") + b"\x20\x01\x00LS"
        image[offset + 8:offset + 8 + len(data)] = data
    # Simulate the two physical boot-record copies while the live API serves1.
    image[79 * 4096 + 8:79 * 4096 + 16] = bytes.fromhex("003d0020badccdab")
    image[80 * 4096:80 * 4096 + 8] = b"\x08\x00\x00\x20\x06\x00LS"
    image[80 * 4096 + 8:80 * 4096 + 16] = NORMAL_BOOTINFO
    return bytes(image)


class FakeInstallUSB(FakePicoUSB):
    """Independent read/write FS model, including erase-on-write-open semantics.

    Faults are applied after the command's real side effect when appropriate;
    a lost acknowledgment can therefore never justify blindly retrying a write.
    Raw storage writes, delete/format/erase, and writes to protected names are
    rejected independently of the production guard.
    """
    def __init__(self, files=None, fault=None, flash_size=1024 * 1024, logical_missing=(), fault_target=None):
        super().__init__(HELPER, flash_size=flash_size)
        self.files = dict(fixture_images() if files is None else files)
        self.original_files = dict(self.files)
        self.logical_missing = set(logical_missing)
        physical = dict(fixture_images(), **self.files)
        self.flash = backup_fixture(physical)
        if flash_size != len(self.flash):
            self.flash = self.flash[:flash_size]
        self.allocated_names = set(physical)
        self.fs_fault = fault
        self.fault_target = fault_target
        self.operation_file = None
        self.fault_used = False
        self.active_file = None
        self.active_mode = None
        self.active_offset = 0
        self.status_context = None
        self.info_queries = []
        self.file_opens = []
        self.file_reads = []
        self.file_closes = []
        self.read_completions = []
        self.mutations = []
        self.write_chunks = []
        self.status_queries = []
        self.before_mutation = None
        self.mutated_names = set()
        self.key_contents_seen_by_host = []

    def hit_fault(self, label):
        if (self.fs_fault == label and not self.fault_used
                and (self.fault_target is None or self.operation_file == self.fault_target)):
            self.fault_used = True
            return True
        return False

    def persistent_event(self, event):
        if self.before_mutation is not None:
            self.before_mutation(event)
        self.mutations.append(dict(event, reads_before=list(self.read_completions)))

    def write(self, packet):
        packet = bytes(packet)
        if packet == ACK or len(packet) < 4 or packet[3] not in (0x21, 0x22, 0x23, 0x24, 0x2a, 0x2b):
            return super().write(packet)
        assert not self._break, "Host sent a command with BREAK asserted"
        expected_length, checksum = struct.unpack(">HB", packet[:3])
        command = packet[3:]
        assert expected_length == len(packet) - 1
        assert checksum == sum(command) & 255
        assert self.helper_running, "Filesystem command before helper startup"
        opcode = command[0]
        self.trace.append(command)

        if opcode == 0x2a:
            length = struct.unpack(">I", command[1:5])[0]
            name = command[5:].decode("ascii")
            assert length == len(command) - 5 and name in ALLOWED_NAMES
            assert self.active_file is None
            self.info_queries.append(name)
            content = None if name in self.logical_missing else self.files.get(name)
            payload = bytes((int(content is not None), 0, 0, 0)) + struct.pack(">I", len(content) if content is not None else 0)
            self.rx.extend(ACK)
            self._reply(payload)
        elif opcode == 0x21:
            flags, token = struct.unpack(">II", command[1:9])
            assert command[-2:] == b"\0\0" and token == 0
            name = command[9:-2].decode("ascii")
            self.operation_file = name
            assert name in ALLOWED_NAMES
            assert self.active_file is None
            assert flags in (0, 0x1000, 0x3001), "Unapproved access/create flags reached device"
            if flags == 0:
                assert name in self.files
            else:
                assert name in (APP_NAME, KEY_NAME), "Protected file write reached device"
                if flags == 0x1000:
                    assert name in self.allocated_names, "Existing-write attempted without an allocation"
                else:
                    assert name == KEY_NAME and name not in self.files, "Create was not a new key"
                self.persistent_event({"kind": "open", "name": name, "flags": flags})
                self.files[name] = b""  # Destructive immediately, even if token/status is lost.
                self.logical_missing.discard(name)
                self.allocated_names.add(name)
                self.mutated_names.add(name)
            self.active_file, self.active_mode, self.active_offset = name, flags, 0
            self.file_opens.append((name, flags))
            self.status_context = "write-open" if flags else "read-open"
            if flags and self.hit_fault("write-open:lost-ack"):
                raise probe.CH340USBError("Simulated USB error after destructive open")
            self.rx.extend(ACK)
            token_reply = bytes.fromhex("12345678")
            self.rx.extend(token_reply[:3] if flags and self.hit_fault("write-open:short-token") else token_reply)
        elif opcode == 0x2b:
            assert len(command) == 9 and self.active_file is not None and self.active_mode == 0
            offset, count = struct.unpack(">II", command[1:])
            assert offset == self.active_offset and 1 <= count <= 256
            content = self.files[self.active_file]
            assert offset + count <= len(content)
            response = content[offset:offset + count]
            self.active_offset += count
            self.file_reads.append((self.active_file, offset, count))
            if self.active_file == KEY_NAME:
                self.key_contents_seen_by_host.append(response)
            fault = None
            if self.active_file in self.mutated_names:
                if self.hit_fault("readback:checksum"):
                    fault = "checksum"
                elif self.hit_fault("readback:short"):
                    fault = "short"
                elif self.hit_fault("readback:mismatch"):
                    response = bytes([response[0] ^ 1]) + response[1:]
            self.rx.extend(ACK)
            self._reply(response, fault=fault)
        elif opcode == 0x24:
            assert len(command) >= 6 and self.active_file in (APP_NAME, KEY_NAME)
            assert self.active_mode in (0x1000, 0x3001)
            offset = struct.unpack(">I", command[1:5])[0]
            payload = command[5:]
            assert offset == self.active_offset and 1 <= len(payload) <= 4096
            self.persistent_event({"kind": "chunk", "name": self.active_file,
                                   "offset": offset, "length": len(payload)})
            self.files[self.active_file] += payload
            self.active_offset += len(payload)
            self.write_chunks.append((self.active_file, offset, payload))
            self.status_context = "chunk"
            if self.hit_fault("chunk:lost-ack"):
                raise probe.CH340USBError("Simulated USB error after file chunk was applied")
            self.rx.extend(ACK)
        elif opcode == 0x22:
            assert command == CLOSE_PACKET and self.active_file is not None
            self.file_closes.append((self.active_file, self.active_mode))
            if self.active_mode == 0:
                assert self.active_offset == len(self.files[self.active_file])
                self.read_completions.append(self.active_file)
                self.status_context = "read-close"
            else:
                self.persistent_event({"kind": "close", "name": self.active_file})
                self.status_context = "write-close"
                if self.hit_fault("write-close:corrupt-content"):
                    content = self.files[self.active_file]
                    self.files[self.active_file] = content[:-1] + bytes([content[-1] ^ 1])
            self.active_file = None
            self.active_mode = None
            if self.status_context == "write-close" and self.hit_fault("write-close:lost-ack"):
                raise probe.CH340USBError("Simulated USB error after file close")
            self.rx.extend(ACK)
        elif opcode == 0x23:
            assert command == b"\x23" and self.status_context is not None
            context = self.status_context
            self.status_queries.append(context)
            self.status_context = None
            self.rx.extend(ACK)
            if self.hit_fault(context + ":bad-status"):
                self._reply(b"\x41")
            elif self.hit_fault(context + ":short-status"):
                self.rx.extend(b"\x00\x03\x40")  # Header claims one byte; payload never arrives.
            elif self.hit_fault(context + ":checksum-status"):
                self._reply(b"\x40", fault="checksum")
            else:
                self._reply(b"\x40")
        return len(packet)


class MemoryJournal:
    def __init__(self):
        self.events = []
    def record(self, event, **fields):
        self.events.append({"event": event, **fields})


class InstallWireTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.files = fixture_images()
        self.original = self.files[APP_NAME]
        self.candidate = candidate_image(installer.CANDIDATE_SIZE)
        self.raw_backup = backup_fixture(self.files)
        self.profile = {
            name: {"size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for name, data in self.files.items() if name != "/sys/mcubootinfo.bin"
        }
        self.backup_path = self.folder / "verified-backup.bin"
        self.key_path = self.folder / "api.key"
        self.candidate_path = self.folder / "standalone.bin"
        for path, data in ((self.backup_path, self.raw_backup), (self.key_path, KEY),
                           (self.candidate_path, self.candidate)):
            path.write_bytes(data)
            path.chmod(0o600)
        self.addCleanup(mock.patch.stopall)
        mock.patch.dict(preflight.EXPECTED_FILES, self.profile, clear=True).start()
        mock.patch.object(installer, "BACKUP_SHA256", hashlib.sha256(self.raw_backup).hexdigest()).start()
        mock.patch.object(installer, "ORIGINAL_SHA256", hashlib.sha256(self.original).hexdigest()).start()
        mock.patch.object(installer, "CANDIDATE_SHA256", hashlib.sha256(self.candidate).hexdigest()).start()
        mock.patch.object(cc.log, "disabled", True).start()
        mock.patch.object(cc.time, "sleep").start()
        mock.patch.object(cc.CC3200Connection, "TIMEOUT", 0.02).start()
        self.report_number = 0

    def output_path(self):
        self.report_number += 1
        return self.folder / f"operation-{self.report_number}.json"

    def cli(self, mode, output):
        argv = [mode, "--backup", str(self.backup_path), "--output", str(output)]
        if mode in ("provision-key", "install"):
            argv += ["--key-file", str(self.key_path)]
        if mode == "install":
            argv += ["--firmware", str(self.candidate_path)]
        return argv

    def run_main(self, fake, mode="provision-key", output=None):
        output = output or self.output_path()
        transcript = io.StringIO()
        port_class = backup.USBSerialPort
        def short_timeout_port(raw):
            port = port_class(raw)
            port.timeout = 0.02
            return port
        with mock.patch.object(probe, "CH340USBTransport", return_value=fake) as transport_factory, \
             mock.patch.object(backup, "QueuedUSBReceive", PassthroughReceive), \
             mock.patch.object(backup, "USBSerialPort", side_effect=short_timeout_port), \
             contextlib.redirect_stdout(transcript), contextlib.redirect_stderr(transcript):
            result = installer.main(self.cli(mode, output))
        manifest = json.loads(output.read_text()) if output.exists() else None
        self.assert_protected_unchanged(fake)
        self.assert_secrets_absent(transcript.getvalue())
        for path in self.folder.iterdir():
            if path.name.startswith("operation-") and path.is_file():
                self.assert_secrets_absent(path.read_text())
        return result, manifest, transcript.getvalue(), transport_factory

    def assert_secrets_absent(self, text):
        for secret in (KEY, OTHER_KEY):
            for representation in (secret.strip().decode(), secret.hex(), secret.strip().hex(),
                                   hashlib.sha256(secret).hexdigest(), hashlib.sha256(secret.strip()).hexdigest()):
                self.assertNotIn(representation, text)

    def assert_protected_unchanged(self, fake):
        for name in PROTECTED:
            self.assertEqual(fake.files.get(name), fake.original_files.get(name), name)
        for command in fake.trace:
            self.assertNotIn(command[0], (0x28, 0x2e, 0x30), command.hex())
            if command[0] == 0x2d:
                self.assertEqual(struct.unpack(">I", command[1:5])[0], 0)
            elif command[0] == 0x21:
                flags, token = struct.unpack(">II", command[1:9])
                self.assertEqual(token, 0)
                if flags != 0:
                    self.assertIn(command[9:-2].decode(), (KEY_NAME, APP_NAME))

    def assert_protected_verified_before_mutations(self, fake, include_main=True):
        required = set(PROTECTED)
        if include_main:
            required.add(APP_NAME)
        for mutation in fake.mutations:
            self.assertTrue(required.issubset(mutation["reads_before"]), mutation)

    def test_local_backup_is_verified_and_original_extracted_before_usb(self):
        original, metadata = installer.verify_backup(self.backup_path)
        self.assertEqual(original, self.original)
        self.assertIsInstance(metadata, dict)
        damaged = bytearray(self.raw_backup)
        damaged[0x8000] ^= 1
        self.backup_path.write_bytes(damaged)
        fake = FakeInstallUSB(self.files)
        result, manifest, transcript, factory = self.run_main(fake)
        self.assertNotEqual(result, 0, transcript)
        factory.assert_not_called()
        self.assertEqual(fake.trace, [])
        self.assertEqual(fake.mutations, [])

    def test_invalid_candidate_cannot_open_usb_or_provision_key(self):
        for bad in (self.candidate[:-1], self.candidate[:-1] + bytes([self.candidate[-1] ^ 1])):
            with self.subTest(length=len(bad)):
                self.candidate_path.write_bytes(bad)
                fake = FakeInstallUSB(self.files)
                result, _, transcript, factory = self.run_main(fake, "install")
                self.assertNotEqual(result, 0, transcript)
                factory.assert_not_called()
                self.assertEqual(fake.mutations, [])

    def test_invalid_key_format_and_permissions_prevent_usb_access(self):
        for value, permissions in ((KEY[:-1], 0o600), (KEY.upper(), 0o600),
                                   (KEY[:-1] + b"\r\n", 0o600), (b"\xff" * 65, 0o600),
                                   (KEY, 0o644)):
            with self.subTest(length=len(value), permissions=oct(permissions)):
                self.key_path.write_bytes(value)
                self.key_path.chmod(permissions)
                fake = FakeInstallUSB(self.files)
                result, _, transcript, factory = self.run_main(fake)
                self.assertNotEqual(result, 0, transcript)
                factory.assert_not_called()
                self.assertEqual(fake.mutations, [])

    def test_new_key_create_then_existing_write_are_both_read_back(self):
        fake = FakeInstallUSB(self.files)
        result, report, transcript, _ = self.run_main(fake)
        self.assertEqual(result, 0, transcript)
        self.assertEqual(fake.files[KEY_NAME], KEY)
        self.assertEqual(fake.files[APP_NAME], self.original)
        self.assertEqual([(event["name"], event["flags"]) for event in fake.mutations if event["kind"] == "open"],
                         [(KEY_NAME, 0x3001), (KEY_NAME, 0x1000)])
        key_read_closes = [(name, mode) for name, mode in fake.file_closes if name == KEY_NAME and mode == 0]
        self.assertGreaterEqual(len(key_read_closes), 2)
        self.assertEqual(b"".join(payload for name, _, payload in fake.write_chunks if name == KEY_NAME), KEY * 2)
        self.assert_protected_verified_before_mutations(fake)
        self.assertTrue(fake.closed)
        self.assertIsNotNone(report)

    def test_identical_key_gets_existing_write_proof_without_create(self):
        fake = FakeInstallUSB({**self.files, KEY_NAME: KEY})
        result, _, transcript, _ = self.run_main(fake)
        self.assertEqual(result, 0, transcript)
        self.assertEqual([(event["name"], event["flags"]) for event in fake.mutations if event["kind"] == "open"],
                         [(KEY_NAME, 0x1000)])
        self.assertEqual(fake.files[KEY_NAME], KEY)
        self.assertEqual(fake.files[APP_NAME], self.original)

    def test_different_existing_key_is_never_overwritten(self):
        for mode in ("provision-key", "install"):
            with self.subTest(mode=mode):
                fake = FakeInstallUSB({**self.files, KEY_NAME: OTHER_KEY})
                result, _, transcript, _ = self.run_main(fake, mode)
                self.assertNotEqual(result, 0, transcript)
                self.assertEqual(fake.mutations, [])
                self.assertEqual(fake.files[KEY_NAME], OTHER_KEY)
                self.assertEqual(fake.files[APP_NAME], self.original)

    def test_live_protected_hash_or_boot_record_mismatch_prevents_all_writes(self):
        for name in (*PROTECTED, APP_NAME):
            with self.subTest(name=name):
                files = dict(self.files)
                files[name] = files[name][:-1] + bytes([files[name][-1] ^ 1])
                fake = FakeInstallUSB(files)
                result, _, transcript, _ = self.run_main(fake)
                self.assertNotEqual(result, 0, transcript)
                self.assertEqual(fake.mutations, [])

    def test_live_extra_application_prevents_all_writes(self):
        fake = FakeInstallUSB({**self.files, "/sys/mcuimg3.bin": b"unreviewed image"})
        result, _, transcript, _ = self.run_main(fake)
        self.assertNotEqual(result, 0, transcript)
        self.assertEqual(fake.mutations, [])

    def test_logically_absent_but_allocated_key_is_not_created(self):
        fake = FakeInstallUSB({**self.files, KEY_NAME: OTHER_KEY}, logical_missing=(KEY_NAME,))
        result, _, transcript, _ = self.run_main(fake)
        self.assertNotEqual(result, 0, transcript)
        self.assertEqual(fake.mutations, [])
        self.assertEqual(fake.files[KEY_NAME], OTHER_KEY)

    def test_ambiguous_or_invalid_fat_never_proves_key_absence(self):
        for fault in ("zero-name-length", "duplicate-key-name", "bad-signature", "main-block-moved", "overlap"):
            with self.subTest(fault=fault):
                fake = FakeInstallUSB({**self.files, KEY_NAME: OTHER_KEY}, logical_missing=(KEY_NAME,))
                raw = bytearray(fake.flash)
                if fault == "zero-name-length":
                    descriptor = 0x774 + 4096 + 20 * 4
                    raw[descriptor + 2:descriptor + 4] = b"\0\0"
                elif fault == "duplicate-key-name":
                    base = 4096
                    raw[base + (21 + 1) * 4:base + (21 + 2) * 4] = bytes((21, 1, 82, 0xc0))
                    descriptor = 0x774 + base
                    raw[descriptor + 21 * 4:descriptor + 22 * 4] = raw[descriptor + 20 * 4:descriptor + 21 * 4]
                elif fault == "bad-signature":
                    raw[2:4] = b"xx"  # Not a genuinely erased alternate FAT.
                elif fault == "main-block-moved":
                    raw[4096 + (18 + 1) * 4 + 2] = 129
                else:
                    raw[4096 + (20 + 1) * 4 + 2] = 130
                fake.flash = bytes(raw)
                result, _, transcript, _ = self.run_main(fake)
                self.assertNotEqual(result, 0, transcript)
                self.assertEqual(fake.mutations, [])

    def test_failed_key_write_never_touches_application_or_retries_chunk(self):
        for fault in ("write-open:lost-ack", "write-open:short-token", "write-open:bad-status",
                      "chunk:lost-ack", "chunk:bad-status", "chunk:short-status", "chunk:checksum-status",
                      "write-close:lost-ack", "write-close:bad-status", "readback:checksum", "readback:short",
                      "readback:mismatch", "write-close:corrupt-content"):
            with self.subTest(fault=fault):
                fake = FakeInstallUSB(self.files, fault=fault)
                result, report, transcript, _ = self.run_main(fake)
                self.assertNotEqual(result, 0, transcript)
                self.assertEqual(fake.files[APP_NAME], self.original)
                self.assertTrue(fake.fault_used, f"Fault was not exercised: {fault}")
                key_offsets = [offset for name, offset, _ in fake.write_chunks if name == KEY_NAME]
                self.assertEqual(len(key_offsets), len(set(key_offsets)), "Ambiguous key chunk was retried")
                self.assertIsNotNone(report, "Failure must leave a reviewable report")
                self.assertTrue(fake.closed)
                self.assert_protected_verified_before_mutations(fake)

    def test_install_writes_only_pinned_application_after_verified_key(self):
        fake = FakeInstallUSB({**self.files, KEY_NAME: KEY})
        result, report, transcript, _ = self.run_main(fake, "install")
        self.assertEqual(result, 0, transcript)
        self.assertEqual(fake.files[APP_NAME], self.candidate)
        self.assertEqual(fake.files[KEY_NAME], KEY)
        application_opens = [event for event in fake.mutations if event["kind"] == "open" and event["name"] == APP_NAME]
        self.assertEqual(len(application_opens), 1)
        self.assertEqual(application_opens[0]["flags"], 0x1000)
        self.assertIn(KEY_NAME, application_opens[0]["reads_before"])
        self.assert_protected_verified_before_mutations(fake)
        application_chunks = [(offset, payload) for name, offset, payload in fake.write_chunks if name == APP_NAME]
        self.assertEqual(b"".join(payload for _, payload in application_chunks), self.candidate)
        position = 0
        for offset, payload in application_chunks:
            self.assertEqual(offset, position)
            position += len(payload)
        self.assertGreaterEqual(fake.read_completions.count(APP_NAME), 2)
        self.assertIsNotNone(report)

    def test_install_rejects_unexpected_live_application_before_any_write(self):
        for content in (b"", self.original[:1024], self.original[:-1] + bytes([self.original[-1] ^ 1])):
            with self.subTest(length=len(content)):
                fake = FakeInstallUSB({**self.files, APP_NAME: content, KEY_NAME: KEY})
                result, _, transcript, _ = self.run_main(fake, "install")
                self.assertNotEqual(result, 0, transcript)
                self.assertEqual(fake.mutations, [])
                self.assertEqual(fake.files[APP_NAME], content)

    def test_install_failure_leaves_journal_and_can_restore_original(self):
        for fault in ("write-open:lost-ack", "chunk:lost-ack", "chunk:bad-status", "readback:mismatch"):
            with self.subTest(fault=fault):
                fake = FakeInstallUSB({**self.files, KEY_NAME: KEY}, fault=fault, fault_target=APP_NAME)
                result, report, transcript, _ = self.run_main(fake, "install")
                self.assertNotEqual(result, 0, transcript)
                self.assertTrue(fake.fault_used)
                self.assertIsNotNone(report)
                application_offsets = [offset for name, offset, _ in fake.write_chunks if name == APP_NAME]
                self.assertEqual(len(application_offsets), len(set(application_offsets)), "Ambiguous application chunk was retried")
                self.assert_protected_verified_before_mutations(fake)
                restored = FakeInstallUSB(fake.files)
                result, restored_report, transcript, _ = self.run_main(restored, "restore")
                self.assertEqual(result, 0, transcript)
                self.assertEqual(restored.files[APP_NAME], self.original)
                self.assertEqual(restored.files[KEY_NAME], KEY)
                self.assertEqual({event["name"] for event in restored.mutations}, {APP_NAME})
                self.assert_protected_verified_before_mutations(restored, include_main=False)
                self.assertIsNotNone(restored_report)

    def test_restore_can_recover_logically_unreadable_main_with_proven_allocation(self):
        fake = FakeInstallUSB({**self.files, APP_NAME: b"", KEY_NAME: OTHER_KEY}, logical_missing=(APP_NAME,))
        result, _, transcript, _ = self.run_main(fake, "restore")
        self.assertEqual(result, 0, transcript)
        self.assertEqual(fake.files[APP_NAME], self.original)
        self.assertEqual(fake.files[KEY_NAME], OTHER_KEY)
        first_write = next(event for event in fake.mutations if event["kind"] == "open")
        self.assertNotIn(APP_NAME, first_write["reads_before"], "Damaged main was opened for read before restoration")
        self.assertEqual(first_write["flags"], 0x1000)
        self.assert_protected_verified_before_mutations(fake, include_main=False)

    def test_restore_requires_exact_main_allocation(self):
        fake = FakeInstallUSB({**self.files, APP_NAME: b""}, logical_missing=(APP_NAME,))
        raw = bytearray(fake.flash)
        raw[4096 + (18 + 1) * 4:4096 + (18 + 2) * 4] = b"\xff" * 4
        fake.flash = bytes(raw)
        result, _, transcript, _ = self.run_main(fake, "restore")
        self.assertNotEqual(result, 0, transcript)
        self.assertEqual(fake.mutations, [])

    def test_restore_refuses_changed_bootloader_recovery_or_selector(self):
        for name in PROTECTED:
            with self.subTest(name=name):
                files = {**self.files, APP_NAME: b""}
                files[name] = files[name][:-1] + bytes([files[name][-1] ^ 1])
                fake = FakeInstallUSB(files)
                result, _, transcript, _ = self.run_main(fake, "restore")
                self.assertNotEqual(result, 0, transcript)
                self.assertEqual(fake.mutations, [])

    def test_readback_requires_full_bytes_even_if_digest_matches(self):
        fake = FakeInstallUSB({**self.files, KEY_NAME: KEY},
                              fault="write-close:corrupt-content", fault_target=APP_NAME)
        corrupted = self.candidate[:-1] + bytes([self.candidate[-1] ^ 1])
        actual_sha256 = hashlib.sha256
        candidate = self.candidate
        class CollidingDigest:
            def __init__(self, data=b"", **_kwargs):
                self.data = bytearray(data)
            def update(self, data):
                self.data.extend(data)
            def hexdigest(self):
                payload = candidate if bytes(self.data) == corrupted else bytes(self.data)
                return actual_sha256(payload).hexdigest()
            def digest(self):
                return bytes.fromhex(self.hexdigest())
        with mock.patch.object(installer.hashlib, "sha256", CollidingDigest):
            result, _, transcript, _ = self.run_main(fake, "install")
        self.assertNotEqual(result, 0, transcript)
        self.assertTrue(fake.fault_used)
        self.assertEqual(fake.files[APP_NAME], corrupted)

    def test_existing_output_prevents_usb_and_preserves_saved_result(self):
        output = self.output_path()
        output.write_text('{"previous":"saved"}\n')
        fake = FakeInstallUSB(self.files)
        result, report, transcript, factory = self.run_main(fake, output=output)
        self.assertNotEqual(result, 0, transcript)
        factory.assert_not_called()
        self.assertEqual(report, {"previous": "saved"})
        self.assertEqual(fake.mutations, [])

    def test_already_installed_exact_candidate_is_verified_without_application_rewrite(self):
        fake = FakeInstallUSB({**self.files, APP_NAME: self.candidate, KEY_NAME: KEY})
        result, report, transcript, _ = self.run_main(fake, "install")
        self.assertEqual(result, 0, transcript)
        self.assertEqual(fake.files[APP_NAME], self.candidate)
        self.assertEqual({event["name"] for event in fake.mutations}, {KEY_NAME})
        self.assertGreaterEqual(fake.read_completions.count(APP_NAME), 2)
        self.assertIsNotNone(report)

    def ready_connection(self, mode="provision-key", files=None, journal=None):
        fake = FakeInstallUSB(files or {**self.files, KEY_NAME: KEY})
        fake.mode = "apps"
        fake.helper_running = True
        journal = journal or MemoryJournal()
        payloads = {APP_NAME: self.original} if mode == "restore" else {KEY_NAME: KEY}
        if mode == "install":
            payloads[APP_NAME] = self.candidate
        connection = installer.build_connection(cc, probe, backup.USBSerialPort(fake), HELPER, mode, payloads, journal)
        connection.helper_sent = len(HELPER)
        connection.helper_executed = True
        connection.flash_size = 1048576
        connection.port.timeout = 0.02
        return connection, fake, journal, payloads

    def seed_proven_state(self, connection, fake):
        # Unit-level guard fixture only; separate end-to-end tests require the
        # actual wire reads before every destructive operation.
        connection.layout = {"application_allocation_verified": True,
                             "image3_absent_in_all_fats": True,
                             "key_absent_in_all_fats": KEY_NAME not in fake.files}
        connection.file_infos = {
            name: {"exists": name in fake.files, "size_bytes": len(fake.files.get(name, b""))}
            for name in ALLOWED_NAMES
        }
        connection.observed = {
            name: {"size_bytes": len(fake.files[name]), "sha256": hashlib.sha256(fake.files[name]).hexdigest()}
            for name in (*PROTECTED, APP_NAME)
        }
        connection.key_matches = fake.files.get(KEY_NAME) == KEY

    def test_write_registration_is_pinned_copied_and_immutable(self):
        connection, fake, _, payloads = self.ready_connection("install")
        payloads[APP_NAME] = b"caller changed original mapping"
        self.assertEqual(connection.payloads[APP_NAME], self.candidate)
        with self.assertRaises(TypeError):
            connection.payloads[APP_NAME] = b"replaced registered bytes"
        with self.assertRaises(AttributeError):
            connection.payloads = {APP_NAME: b"replacement registry"}
        for mode, values in (("install", {KEY_NAME: KEY, APP_NAME: self.original}),
                             ("provision-key", {KEY_NAME: KEY, APP_NAME: self.candidate}),
                             ("restore", {APP_NAME: self.candidate}),
                             ("provision-key", {KEY_NAME: bytearray(KEY)})):
            with self.subTest(mode=mode):
                with self.assertRaises(installer.InstallError):
                    installer.build_connection(cc, probe, backup.USBSerialPort(fake), HELPER, mode, values, MemoryJournal())
        self.assertEqual(fake.trace, [])

    def test_write_opens_require_completed_live_proofs_and_explicit_arming(self):
        for mode, target, flags in (("provision-key", KEY_NAME, 0x1000),
                                    ("install", APP_NAME, 0x1000),
                                    ("restore", APP_NAME, 0x1000)):
            with self.subTest(mode=mode):
                connection, fake, _, _ = self.ready_connection(mode)
                with self.assertRaises(installer.InstallError):
                    connection.arm_write(target, flags)
                self.seed_proven_state(connection, fake)
                connection.key_existing_write_verified = True
                command = b"\x21" + struct.pack(">II", flags, 0) + target.encode() + b"\0\0"
                with self.assertRaises(installer.InstallError):
                    connection._send_packet(command)
                self.assertEqual(fake.trace, [])

    def test_guard_blocks_protected_files_delete_erase_and_wrong_access_modes(self):
        commands = [b"\x28", b"\x2e" + b"\0" * 4 + APP_NAME.encode() + b"\0",
                    b"\x30" + struct.pack(">III", 2, 0, 1),
                    b"\x2d" + struct.pack(">III", 2, 0, 1) + b"x"]
        for name in PROTECTED:
            for flags in (0x1000, 0x3001):
                commands.append(b"\x21" + struct.pack(">II", flags, 0) + name.encode() + b"\0\0")
        for flags in (1, 0x2000, 0x3000, 0x10000, 0xffffffff):
            commands.append(b"\x21" + struct.pack(">II", flags, 0) + KEY_NAME.encode() + b"\0\0")
        for command in commands:
            with self.subTest(opcode=hex(command[0])):
                connection, fake, _, _ = self.ready_connection("install")
                self.seed_proven_state(connection, fake)
                with self.assertRaises(backup.BackupError):
                    connection._send_packet(command)
                self.assertEqual(fake.trace, [])
        for name in PROTECTED:
            connection, fake, _, _ = self.ready_connection("install")
            self.seed_proven_state(connection, fake)
            with self.assertRaises(installer.InstallError):
                connection.arm_write(name, 0x1000)
            self.assertEqual(fake.trace, [])

    def test_guard_only_accepts_registered_sequential_bytes_and_waits_for_status(self):
        connection, fake, _, _ = self.ready_connection()
        self.seed_proven_state(connection, fake)
        connection.arm_write(KEY_NAME, 0x1000)
        connection._open_file(KEY_NAME, 0x1000)
        for offset, data in ((1, KEY), (0, KEY[:-1] + b"x"), (0, KEY + b"x"), (0, b"")):
            before = list(fake.trace)
            with self.assertRaises(installer.InstallError):
                connection._send_packet(b"\x24" + struct.pack(">I", offset) + data)
            self.assertEqual(fake.trace, before)
        with self.assertRaises(installer.InstallError):
            connection._send_packet(CLOSE_PACKET)
        command = b"\x24" + struct.pack(">I", 0) + KEY
        connection._send_packet(command)
        before = list(fake.trace)
        with self.assertRaises(installer.InstallError):
            connection._send_packet(command)
        self.assertEqual(fake.trace, before)
        self.assertTrue(connection._get_last_status().is_ok)
        connection._close_file()
        self.assertEqual(fake.files[KEY_NAME], KEY)
        self.assertFalse(connection.key_existing_write_verified)
        self.assertEqual(installer.read_file_bytes(connection, KEY_NAME), KEY)
        self.assertTrue(connection.key_existing_write_verified)

    def test_journal_intent_is_on_disk_before_destructive_open(self):
        output = self.output_path()
        fake = FakeInstallUSB(self.files)
        observations = []
        def before_mutation(event):
            if event["kind"] != "open":
                return
            journal_path = Path(str(output) + ".journal.jsonl")
            records = [json.loads(line) for line in journal_path.read_text().splitlines()]
            last = records[-1]
            self.assertEqual(last["event"], "before-write-open")
            self.assertEqual(last["target"], event["name"])
            self.assertEqual(last["mode"], f"0x{event['flags']:04x}")
            observations.append(last)
        fake.before_mutation = before_mutation
        result, _, transcript, _ = self.run_main(fake, output=output)
        self.assertEqual(result, 0, transcript)
        self.assertEqual(len(observations), 2)

    def test_journal_failures_poison_connection_and_prevent_ambiguous_retries(self):
        for failure_event in ("before-write-open", "write-open-acknowledged", "write-chunk-status-ok"):
            with self.subTest(event=failure_event):
                class FailingJournal(MemoryJournal):
                    def record(self, event, **fields):
                        if event == failure_event:
                            raise OSError("Simulated journal flush failure")
                        super().record(event, **fields)
                connection, fake, journal, _ = self.ready_connection(journal=FailingJournal())
                self.seed_proven_state(connection, fake)
                with self.assertRaises(installer.InstallError):
                    installer._write_registered(connection, KEY_NAME, 0x1000)
                self.assertTrue(connection.failed)
                before = list(fake.trace)
                with self.assertRaises(installer.InstallError):
                    connection._send_packet(b"\x2f")
                self.assertEqual(fake.trace, before)
                if failure_event == "before-write-open":
                    self.assertEqual(fake.mutations, [])
                    self.assertEqual(fake.files[KEY_NAME], KEY)
                elif failure_event == "write-open-acknowledged":
                    self.assertEqual(fake.files[KEY_NAME], b"")
                    self.assertEqual(fake.write_chunks, [])
                else:
                    self.assertEqual(len(fake.write_chunks), 1)

    def test_transport_errors_never_expose_received_key_bytes(self):
        class LeakyTransport(FakeInstallUSB):
            def write(self, packet):
                result = super().write(packet)
                if len(packet) >= 4 and packet[3] == 0x24:
                    raise probe.CH340USBError(KEY.decode())
                return result
        fake = LeakyTransport(self.files)
        result, report, transcript, _ = self.run_main(fake)
        self.assertNotEqual(result, 0, transcript)
        self.assertIsNotNone(report)
        self.assert_secrets_absent(transcript + json.dumps(report))

    def test_malformed_fat_names_do_not_leak_through_upstream_logging(self):
        fake = FakeInstallUSB(self.files)
        raw = bytearray(fake.flash)
        entries = [(0, "/sys/mcuimg.bin", 71, 4, True),
                   (9, "/sys/mcubootinfo.bin", 79, 1, True),
                   (10, "/sys/mcuimg1.bin", 214, 16, False),
                   (18, APP_NAME, 130, 48, False),
                   (22, KEY.strip().decode(), 130, 1, False)]
        add_fat(raw, 1, 225, entries)
        fake.flash = bytes(raw)
        logs = io.StringIO()
        handler = logging.StreamHandler(logs)
        old_level = cc.log.level
        cc.log.addHandler(handler)
        cc.log.setLevel(logging.DEBUG)
        try:
            with mock.patch.object(cc.log, "disabled", False):
                result, _, transcript, _ = self.run_main(fake)
        finally:
            cc.log.removeHandler(handler)
            cc.log.setLevel(old_level)
        self.assertNotEqual(result, 0, transcript)
        self.assertEqual(fake.mutations, [])
        self.assert_secrets_absent(logs.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
