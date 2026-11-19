#!/usr/bin/env python3
"""Portable recovery state machine against a destructive/faulting filesystem.

No USB or physical device access. The native bridge has a separate test suite.
"""

import ctypes as C
import gzip
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
OK, ARGUMENT, LOCKED, BUSY, PLAN, NOT_FOUND, CAPACITY, SESSION, OFFSET, CONTENT, CHECKSUM, IO, INCOMPLETE, STATE = range(14)
STAT, OPEN_READ, OPEN_WRITE, CREATE, READ, WRITE, CLOSE, BOOT_READ, BOOT_WRITE = range(9)
IMAGE2, IMAGE3, WEB_I, WEB_A, WEB_E, WEB_H, WEB_S = range(7)
MAX_IMAGE = 188416
NORMAL = bytes.fromhex("003d0020badccdab")


def image(length=2048, seed=7, sp=0x20038000, reset=0x20004009):
    payload = bytes((i * 37 + seed) & 255 for i in range(max(length - 8, 0)))
    return (struct.pack("<II", sp, reset) + payload)[:length]


def cbytes(data):
    return (C.c_ubyte * len(data)).from_buffer_copy(data)


class RecoveryCoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="pico-recovery-core-")
        output = Path(cls.temp.name) / "recovery.so"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-shared", "-fPIC", "-I", str(ROOT / "recovery"),
            str(ROOT / "recovery/recovery.c"), str(ROOT / "recovery/sha256.c"),
            str(ROOT / "tests/recovery_core_host.c"), "-o", str(output),
        ], check=True)
        cls.lib = C.CDLL(str(output))
        u, p, z = C.c_uint32, C.c_void_p, C.c_size_t
        specs = {
            "rf_reset": ([u], None), "rf_reboot": ([u], None),
            "rf_set_file": ([u, p, u, u], None), "rf_remove_file": ([u], None),
            "rf_copy_file": ([u, p, u], u), "rf_file_size": ([u], u),
            "rf_file_allocation": ([u], u), "rf_set_boot": ([p], None),
            "rf_get_boot": ([p], None), "rf_plan": ([u, C.c_char_p, C.c_int, p, p], C.c_int),
            "rf_mutate_request_length": ([u], None), "rf_begin": ([u, u], C.c_int),
            "rf_chunk": ([u, u, u, p, z], C.c_int), "rf_finish": ([u, u], C.c_int),
            "rf_abort": ([u], C.c_int), "rf_verify": ([u, u], C.c_int),
            "rf_select": ([u, C.c_int], C.c_int), "rf_session": ([], u),
            "rf_written": ([], u), "rf_active": ([], C.c_int),
            "rf_committed": ([], C.c_int), "rf_verified": ([u], C.c_int),
            "rf_inventory": ([u, p, p, p], C.c_int), "rf_fault": ([u, u, u], None),
            "rf_boot_change": ([u], None), "rf_count": ([u], u),
            "rf_violations": ([], u), "rf_live_handles": ([], u),
            "rf_hash": ([p, z, z, p], None),
        }
        for name, (arguments, result) in specs.items():
            getattr(cls.lib, name).argtypes = arguments
            getattr(cls.lib, name).restype = result

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.rf_reset(0x10203040)
        self.old = image(512, seed=1)
        self.put(IMAGE2, self.old, MAX_IMAGE)
        self.target = image()
        self.web = [gzip.compress((f"asset{i}:" * 30).encode(), mtime=0) for i in range(5)]

    def tearDown(self):
        self.assertEqual(self.lib.rf_violations(), 0, "core attempted an unguarded/protected mutation")
        self.assertEqual(self.lib.rf_live_handles(), 0, "file handle leaked")

    def put(self, file, data, allocation=None):
        self.lib.rf_set_file(file, cbytes(data), len(data), allocation or len(data))

    def content(self, file):
        size = self.lib.rf_file_size(file)
        result = (C.c_ubyte * size)()
        self.assertEqual(self.lib.rf_copy_file(file, result, size), size)
        return bytes(result)

    def boot(self):
        result = (C.c_ubyte * 8)()
        self.lib.rf_get_boot(result)
        return bytes(result)

    def plan(self, *, slot=2, target=None, web=None, lengths=None, version=b"1.0.0"):
        artifacts = [self.target if target is None else target] + (web if web is not None else [b""] * 5)
        sizes = lengths if lengths is not None else [len(data) for data in artifacts]
        length_array = (C.c_uint32 * 6)(*sizes)
        hashes = cbytes(b"".join(hashlib.sha256(data).digest() for data in artifacts))
        return self.lib.rf_plan(slot, version, web is not None, length_array, hashes)

    @property
    def session(self):
        return self.lib.rf_session()

    def chunk(self, file, offset, data, session=None):
        return self.lib.rf_chunk(self.session if session is None else session,
                                 file, offset, cbytes(data), len(data))

    def upload(self, file, data):
        self.assertEqual(self.lib.rf_begin(self.session, file), OK)
        for offset in range(0, len(data), 1024):
            self.assertEqual(self.chunk(file, offset, data[offset:offset + 1024]), OK)
        self.assertEqual(self.lib.rf_finish(self.session, file), OK)

    def assert_no_mutation(self):
        for operation in (OPEN_WRITE, CREATE, WRITE, BOOT_WRITE):
            self.assertEqual(self.lib.rf_count(operation), 0)

    def test_sha256_known_vectors_and_padding_boundaries(self):
        cases = [b"", b"abc", b"a" * 1_000_000]
        cases += [bytes(range(n)) for n in (1, 55, 56, 63, 64, 65, 127, 128, 129)]
        for data in cases:
            for stride in (0, 1, 17, 64, 509):
                with self.subTest(size=len(data), stride=stride):
                    digest = (C.c_ubyte * 32)()
                    self.lib.rf_hash(cbytes(data), len(data), stride, digest)
                    self.assertEqual(bytes(digest), hashlib.sha256(data).digest())
        self.assertEqual(hashlib.sha256(b"abc").hexdigest(),
                         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        self.assert_no_mutation()

    def test_inventory_identifies_absent_slot_without_creating_it(self):
        size, allocation, writable = C.c_uint32(), C.c_uint32(), C.c_int()
        self.assertEqual(self.lib.rf_inventory(IMAGE3, C.byref(size), C.byref(allocation),
                                              C.byref(writable)), 1)
        self.assertEqual(writable.value, 1)
        self.assertEqual(self.lib.rf_inventory(IMAGE2, C.byref(size), C.byref(allocation),
                                              C.byref(writable)), 0)
        self.assertEqual((size.value, allocation.value), (len(self.old), MAX_IMAGE))
        self.assert_no_mutation()

    def test_invalid_plan_bounds_and_version_have_no_storage_effect(self):
        for slot in (0, 1, 4, 0xffffffff):
            self.assertEqual(self.plan(slot=slot), PLAN)
        for length in (0, 8, 15, MAX_IMAGE + 1, 0xffffffff):
            self.assertEqual(self.plan(lengths=[length, 0, 0, 0, 0, 0]), PLAN)
        for version in (b"", b"1\r\nBad", b"../file", b'a"b', b"a" * 32):
            self.assertEqual(self.plan(version=version), PLAN)
        for file_index, limit in enumerate((7680, 7680, 3584, 7680, 3584), 1):
            lengths = [len(self.target)] + [len(data) for data in self.web]
            for invalid in (0, 9, limit + 1, 0xffffffff):
                lengths[file_index] = invalid
                self.assertEqual(self.plan(web=self.web, lengths=lengths), PLAN)
        self.assert_no_mutation()

    def test_absent_application_never_uses_create(self):
        self.assertEqual(self.plan(slot=3, web=self.web), NOT_FOUND)
        self.assert_no_mutation()
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.lib.rf_remove_file(IMAGE2)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), NOT_FOUND)
        self.assertEqual(self.lib.rf_count(CREATE), 0)

    def test_plan_checks_live_application_and_web_allocations(self):
        self.put(IMAGE2, self.old, 1024)
        self.assertEqual(self.plan(), CAPACITY)
        self.put(IMAGE2, self.old, MAX_IMAGE)
        self.put(WEB_H, b"old", len(self.web[3]) - 1)
        self.assertEqual(self.plan(web=self.web), CAPACITY)
        self.assertEqual(self.content(IMAGE2), self.old)
        self.assert_no_mutation()

    def test_plan_stat_failure_and_invalid_info_fail_closed(self):
        self.lib.rf_fault(STAT, 1, 1)
        self.assertEqual(self.plan(), IO)
        self.put(IMAGE2, self.old, 1)
        self.assertEqual(self.plan(), IO)
        self.assert_no_mutation()

    def test_plan_is_copied_and_session_replaced(self):
        self.assertEqual(self.plan(), OK)
        first = self.session
        self.lib.rf_mutate_request_length(0xffffffff)
        self.upload(IMAGE2, self.target)
        self.assertEqual(self.plan(), OK)
        self.assertNotEqual(self.session, first)
        self.assertEqual(self.lib.rf_begin(first, IMAGE2), SESSION)
        self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), OK)

    def test_session_counter_wrap_skips_zero(self):
        self.lib.rf_reset(0xffffffff)
        self.put(IMAGE2, self.old, MAX_IMAGE)
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.session, 1)
        self.assertEqual(self.lib.rf_begin(0, IMAGE2), SESSION)
        self.assert_no_mutation()

    def test_normal_upload_verify_and_select_preserve_reserved_boot_bytes(self):
        record = b"\x00\xaa\x55\xff" + NORMAL[4:]
        self.lib.rf_set_boot(cbytes(record))
        self.assertEqual(self.plan(), OK)
        self.upload(IMAGE2, self.target)
        self.assertEqual(self.content(IMAGE2), self.target)
        self.assertEqual(self.lib.rf_verified(IMAGE2), 1)
        read_before = self.lib.rf_count(READ)
        self.assertEqual(self.lib.rf_select(self.session, 0), OK)
        self.assertGreater(self.lib.rf_count(READ), read_before)
        self.assertEqual(self.boot(), b"\x01" + record[1:])
        self.assertEqual(self.lib.rf_committed(), 1)
        self.assertEqual(self.lib.rf_count(BOOT_WRITE), 1)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), STATE)
        self.assertEqual(self.lib.rf_select(self.session, 0), STATE)

    def test_image3_can_be_rewritten_only_if_allocation_already_exists(self):
        original3 = image(100, seed=9)
        self.put(IMAGE3, original3, MAX_IMAGE)
        self.assertEqual(self.plan(slot=3), OK)
        self.upload(IMAGE3, self.target)
        self.assertEqual(self.content(IMAGE2), self.old)
        self.assertEqual(self.lib.rf_select(self.session, 1), PLAN)
        self.assertEqual(self.lib.rf_count(BOOT_WRITE), 0)
        self.assertEqual(self.lib.rf_select(self.session, 0), OK)
        self.assertEqual(self.boot()[0], 2)
        self.assertEqual(self.lib.rf_count(CREATE), 0)

    def test_one_shot_image2_trial_retains_recovery_selector(self):
        self.put(IMAGE2, self.target, MAX_IMAGE)
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_select(self.session, 1), OK)
        self.assertEqual(self.boot(), NORMAL[:4] + bytes.fromhex("65877856"))
        self.assertEqual(self.lib.rf_committed(), 1)
        self.assertEqual(self.lib.rf_count(OPEN_WRITE), 0)

    def test_all_five_web_assets_can_be_created_then_verified(self):
        self.assertEqual(self.plan(web=self.web), OK)
        self.assertEqual(self.lib.rf_verify(self.session, WEB_I), NOT_FOUND)
        for file, data, allocation in zip(range(WEB_I, WEB_S + 1), self.web,
                                           (7680, 7680, 3584, 7680, 3584)):
            self.upload(file, data)
            self.assertEqual(self.lib.rf_file_allocation(file), allocation)
            self.assertEqual(self.content(file), data)
        self.upload(IMAGE2, self.target)
        self.assertEqual(self.lib.rf_count(CREATE), 5)
        self.assertEqual(self.lib.rf_select(self.session, 0), OK)

    def test_select_checks_all_planned_web_files_even_after_prior_verification(self):
        self.put(IMAGE2, self.target, MAX_IMAGE)
        for file, data in zip(range(WEB_I, WEB_S + 1), self.web):
            self.put(file, data, 7680 if file not in (WEB_E, WEB_S) else 3584)
        self.assertEqual(self.plan(web=self.web), OK)
        for file in (IMAGE2, *range(WEB_I, WEB_S + 1)):
            self.assertEqual(self.lib.rf_verify(self.session, file), OK)
        corrupt = self.web[-1][:-1] + bytes((self.web[-1][-1] ^ 1,))
        self.put(WEB_S, corrupt, 3584)
        self.assertEqual(self.lib.rf_select(self.session, 0), CHECKSUM)
        self.assertEqual(self.lib.rf_count(BOOT_WRITE), 0)
        self.assertEqual(self.lib.rf_verified(WEB_S), 0)

    def test_select_cannot_omit_a_missing_planned_web_file(self):
        self.put(IMAGE2, self.target, MAX_IMAGE)
        self.assertEqual(self.plan(web=self.web), OK)
        self.assertEqual(self.lib.rf_select(self.session, 0), NOT_FOUND)
        self.assert_no_mutation()

    def test_invalid_vectors_are_rejected_before_destructive_open(self):
        cases = [image(sp=0x20038004), image(sp=0x20004000), image(sp=0x20040008),
                 image(reset=0x20004008), image(reset=0x1fffffff),
                 image(reset=0x20004001), image(reset=0x20004801)]
        for bad in cases:
            with self.subTest(vectors=bad[:8].hex()):
                self.assertEqual(self.plan(target=bad), OK)
                self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
                self.assertEqual(self.chunk(IMAGE2, 0, bad[:1024]), CONTENT)
                self.assertEqual(self.content(IMAGE2), self.old)
        self.assert_no_mutation()

    def test_valid_top_of_ram_stack_and_tiny_first_chunk(self):
        target = image(sp=0x20040000)
        self.assertEqual(self.plan(target=target), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, target[:7]), CONTENT)
        self.assert_no_mutation()
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, target[:8]), OK)
        offset = 8
        while offset < len(target):
            data = target[offset:offset + 1024]
            self.assertEqual(self.chunk(IMAGE2, offset, data), OK)
            offset += len(data)
        self.assertEqual(self.lib.rf_finish(self.session, IMAGE2), OK)

    def test_stack_overlaps_image_or_lacks_first_push_space_before_erase(self):
        for stack in (0x20004008, 0x20004400, 0x20004800):
            with self.subTest(stack=hex(stack)):
                target = image(2048, sp=stack)
                self.assertEqual(self.plan(target=target), OK)
                self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
                self.assertEqual(self.chunk(IMAGE2, 0, target[:1024]), CONTENT)
                self.assertEqual(self.content(IMAGE2), self.old)
                self.assertEqual(self.lib.rf_count(OPEN_WRITE), 0)
        self.assert_no_mutation()

    def test_readback_rejects_stack_inside_or_at_end_of_loaded_image(self):
        for stack in (0x20004008, 0x20004400, 0x20004800):
            with self.subTest(stack=hex(stack)):
                target = image(2048, sp=stack)
                self.put(IMAGE2, target, MAX_IMAGE)
                self.assertEqual(self.plan(target=target), OK)
                self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), CONTENT)
                self.assertEqual(self.lib.rf_select(self.session, 0), CONTENT)
                self.assertEqual(self.lib.rf_verified(IMAGE2), 0)
        self.assert_no_mutation()

    def test_truncated_final_thumb_halfword_rejected_before_erase_and_on_readback(self):
        target = image(2049, reset=0x20004801)
        self.assertEqual(self.plan(target=target), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, target[:1024]), CONTENT)
        self.assertEqual(self.content(IMAGE2), self.old)
        self.put(IMAGE2, target, MAX_IMAGE)
        self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), CONTENT)
        self.assertEqual(self.lib.rf_select(self.session, 0), CONTENT)
        self.assert_no_mutation()

    def test_minimum_stack_gap_and_complete_final_thumb_halfword_are_accepted(self):
        target = image(2048, sp=0x20004808, reset=0x200047ff)
        target = target[:-2] + b"\x00\xbf"  # One complete Thumb NOP at the end.
        self.assertEqual(self.plan(target=target), OK)
        self.upload(IMAGE2, target)
        self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), OK)

    def test_large_reset_address_cannot_wrap_halfword_bound(self):
        target = image(reset=0xffffffff)
        self.assertEqual(self.plan(target=target), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, target[:1024]), CONTENT)
        self.put(IMAGE2, target, MAX_IMAGE)
        self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), CONTENT)
        self.assert_no_mutation()

    def test_bad_gzip_headers_never_create_or_erase(self):
        for bad_header in (b"wrong magic", b"\x1f\x8b\x07\0" + b"\0" * 6,
                           b"\x1f\x8b\x08\xe0" + b"\0" * 6):
            web = list(self.web)
            web[0] = bad_header + b"payload"
            self.assertEqual(self.plan(web=web), OK)
            self.assertEqual(self.lib.rf_begin(self.session, WEB_I), OK)
            self.assertEqual(self.chunk(WEB_I, 0, web[0]), CONTENT)
        self.assertEqual(self.plan(web=self.web), OK)
        self.assertEqual(self.lib.rf_begin(self.session, WEB_I), OK)
        self.assertEqual(self.chunk(WEB_I, 0, self.web[0][:9]), CONTENT)
        self.assert_no_mutation()

    def test_exact_offsets_size_limits_and_single_upload_handle(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), BUSY)
        self.assertEqual(self.plan(), BUSY)
        self.assertEqual(self.chunk(IMAGE2, 0, b""), ARGUMENT)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1025]), ARGUMENT)
        self.assertEqual(self.chunk(IMAGE2, 1, self.target[:8]), OFFSET)
        self.assertEqual(self.chunk(IMAGE2, 0xffffffff, self.target[:8]), OFFSET)
        self.assertEqual(self.chunk(IMAGE3, 0, self.target[:8]), STATE)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:8], self.session + 1), SESSION)
        self.assert_no_mutation()
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OFFSET)
        self.assertEqual(self.chunk(IMAGE2, 1024, self.target[1024:]), OK)
        self.assertEqual(self.chunk(IMAGE2, len(self.target), b"x"), OFFSET)
        self.assertEqual(self.lib.rf_finish(self.session, IMAGE2), OK)

    def test_unplanned_or_invalid_file_enum_is_rejected(self):
        self.assertEqual(self.plan(), OK)
        for file in (IMAGE3, WEB_I, WEB_S, 7, 0xffffffff):
            self.assertEqual(self.lib.rf_begin(self.session, file), PLAN)
            self.assertEqual(self.lib.rf_verify(self.session, file), PLAN)
        self.assert_no_mutation()

    def test_incomplete_finish_closes_and_invalidates(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
        self.assertEqual(self.lib.rf_finish(self.session, IMAGE2), INCOMPLETE)
        self.assertEqual(self.lib.rf_active(), 0)
        self.assertEqual(self.lib.rf_verified(IMAGE2), 0)
        self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), INCOMPLETE)
        self.assertEqual(self.boot(), NORMAL)

    def test_abort_closes_partial_file_and_never_marks_it_verified(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
        self.assertEqual(self.lib.rf_abort(self.session), OK)
        self.assertEqual(self.lib.rf_file_size(IMAGE2), 1024)
        self.assertEqual(self.lib.rf_verified(IMAGE2), 0)
        self.assertEqual(self.lib.rf_active(), 0)
        self.assertEqual(self.lib.rf_abort(self.session), OK)
        self.assertEqual(self.lib.rf_count(CLOSE), 1)

    def test_write_error_partial_write_and_open_failures_never_retry(self):
        for operation, mode in ((OPEN_WRITE, 1), (OPEN_WRITE, 2), (WRITE, 1), (WRITE, 2)):
            with self.subTest(operation=operation, mode=mode):
                self.setUp()
                self.assertEqual(self.plan(), OK)
                self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
                self.lib.rf_fault(operation, 1, mode)
                self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), IO)
                self.assertEqual(self.lib.rf_count(operation), 1)
                self.assertEqual(self.lib.rf_live_handles(), 0)
                self.assertEqual(self.lib.rf_active(), 0)
                self.assertEqual(self.lib.rf_verified(IMAGE2), 0)
                self.assertEqual(self.lib.rf_count(CREATE), 0)
                self.assertEqual(self.boot(), NORMAL)

    def test_failed_web_creation_is_not_retried_as_another_operation(self):
        for mode in (1, 2):
            self.setUp()
            self.assertEqual(self.plan(web=self.web), OK)
            self.assertEqual(self.lib.rf_begin(self.session, WEB_I), OK)
            self.lib.rf_fault(CREATE, 1, mode)
            self.assertEqual(self.chunk(WEB_I, 0, self.web[0]), IO)
            self.assertEqual(self.lib.rf_count(CREATE), 1)
            self.assertEqual(self.lib.rf_count(OPEN_WRITE), 0)
            self.assertEqual(self.lib.rf_count(WRITE), 0)
            self.assertEqual(self.lib.rf_live_handles(), 0)

    def test_failed_existing_web_open_never_falls_back_to_create(self):
        self.put(WEB_I, self.web[0], 7680)
        self.assertEqual(self.plan(web=self.web), OK)
        self.assertEqual(self.lib.rf_begin(self.session, WEB_I), OK)
        self.lib.rf_fault(OPEN_WRITE, 1, 1)
        self.assertEqual(self.chunk(WEB_I, 0, self.web[0]), IO)
        self.assertEqual(self.lib.rf_count(CREATE), 0)
        self.assertEqual(self.content(WEB_I), self.web[0])

    def test_finish_close_failure_does_not_double_close_or_verify(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
        self.assertEqual(self.chunk(IMAGE2, 1024, self.target[1024:]), OK)
        self.lib.rf_fault(CLOSE, 1, 1)
        self.assertEqual(self.lib.rf_finish(self.session, IMAGE2), IO)
        self.assertEqual(self.lib.rf_count(CLOSE), 1)
        self.assertEqual(self.lib.rf_count(READ), 0)
        self.assertEqual(self.lib.rf_verified(IMAGE2), 0)

    def test_readback_catches_write_corruption_and_vectors_changed_on_close(self):
        for operation, mode, expected in ((WRITE, 3, CHECKSUM), (CLOSE, 2, CONTENT)):
            self.setUp()
            self.assertEqual(self.plan(), OK)
            self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
            self.lib.rf_fault(operation, 1, mode)
            self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
            self.assertEqual(self.chunk(IMAGE2, 1024, self.target[1024:]), OK)
            self.assertEqual(self.lib.rf_finish(self.session, IMAGE2), expected)
            self.assertEqual(self.lib.rf_verified(IMAGE2), 0)
            self.assertEqual(self.lib.rf_count(BOOT_WRITE), 0)
            self.assertEqual(self.lib.rf_live_handles(), 0)

    def test_verification_io_errors_always_close_and_clear_verified_state(self):
        for operation, after, mode in ((STAT, 1, 1), (OPEN_READ, 1, 1),
                                        (READ, 1, 1), (READ, 2, 2), (CLOSE, 1, 1)):
            self.setUp()
            self.put(IMAGE2, self.target, MAX_IMAGE)
            self.assertEqual(self.plan(), OK)
            self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), OK)
            self.lib.rf_fault(operation, after, mode)
            self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), IO)
            self.assertEqual(self.lib.rf_verified(IMAGE2), 0)
            self.assertEqual(self.lib.rf_live_handles(), 0)
            self.assert_no_mutation()

    def test_guard_rejects_all_other_boot_states(self):
        for record in (b"\x01" + NORMAL[1:], b"\x02" + NORMAL[1:], b"\0" * 8,
                       NORMAL[:4] + bytes.fromhex("65877856"),
                       NORMAL[:4] + bytes.fromhex("21433412")):
            self.lib.rf_set_boot(cbytes(record))
            self.assertEqual(self.plan(), LOCKED)
        self.assert_no_mutation()

    def test_guard_is_rechecked_before_open_and_before_each_write(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.lib.rf_boot_change(1)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), LOCKED)
        self.assert_no_mutation()
        self.assertEqual(self.content(IMAGE2), self.old)
        self.setUp()
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.lib.rf_boot_change(3)  # After destructive open, before first data write.
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), LOCKED)
        self.assertEqual(self.lib.rf_count(OPEN_WRITE), 1)
        self.assertEqual(self.lib.rf_count(WRITE), 0)
        self.assertEqual(self.lib.rf_count(CLOSE), 1)
        self.assertEqual(self.lib.rf_active(), 0)

    def test_later_guard_failure_stops_writes_and_cleans_up(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
        self.lib.rf_set_boot(cbytes(b"\x01" + NORMAL[1:]))
        self.assertEqual(self.chunk(IMAGE2, 1024, self.target[1024:]), LOCKED)
        self.assertEqual(self.lib.rf_count(WRITE), 1)
        self.assertEqual(self.lib.rf_count(CLOSE), 1)
        self.assertEqual(self.lib.rf_active(), 0)
        self.assertEqual(self.lib.rf_verified(IMAGE2), 0)

    def test_guard_read_failure_also_closes_an_open_upload(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
        self.lib.rf_fault(BOOT_READ, 1, 1)
        self.assertEqual(self.chunk(IMAGE2, 1024, self.target[1024:]), IO)
        self.assertEqual(self.lib.rf_count(CLOSE), 1)
        self.assertEqual(self.lib.rf_count(WRITE), 1)
        self.assertEqual(self.lib.rf_active(), 0)
        self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), INCOMPLETE)

    def test_inventory_io_failure_closes_an_open_upload(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), OK)
        self.lib.rf_fault(STAT, 1, 1)
        size, allocation, writable = C.c_uint32(), C.c_uint32(), C.c_int()
        self.assertLess(self.lib.rf_inventory(IMAGE2, C.byref(size), C.byref(allocation),
                                             C.byref(writable)), 0)
        self.assertEqual(self.lib.rf_count(CLOSE), 1)
        self.assertEqual(self.lib.rf_active(), 0)

    def test_capacity_is_rechecked_immediately_before_open(self):
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_begin(self.session, IMAGE2), OK)
        self.put(IMAGE2, self.old, 1024)
        self.assertEqual(self.chunk(IMAGE2, 0, self.target[:1024]), CAPACITY)
        self.assertEqual(self.content(IMAGE2), self.old)
        self.assert_no_mutation()

    def test_boot_commit_failure_never_claims_success_or_retries(self):
        for mode in (1, 2, 3, 4, 5):
            with self.subTest(mode=mode):
                self.setUp()
                self.put(IMAGE2, self.target, MAX_IMAGE)
                self.assertEqual(self.plan(), OK)
                self.lib.rf_fault(BOOT_WRITE, 1, mode)
                self.assertEqual(self.lib.rf_select(self.session, 0), IO)
                self.assertEqual(self.lib.rf_committed(), 0)
                self.assertEqual(self.lib.rf_verified(IMAGE2), 0)
                self.assertEqual(self.lib.rf_count(BOOT_WRITE), 1)
                self.assertEqual(self.lib.rf_live_handles(), 0)

    def test_boot_readback_failure_does_not_report_commit(self):
        self.put(IMAGE2, self.target, MAX_IMAGE)
        self.assertEqual(self.plan(), OK)
        self.lib.rf_fault(BOOT_READ, 3, 1)  # verify guard, final guard, readback
        self.assertEqual(self.lib.rf_select(self.session, 0), IO)
        self.assertEqual(self.lib.rf_count(BOOT_WRITE), 1)
        self.assertEqual(self.boot()[0], 1)  # Persistence is uncertain to the caller.
        self.assertEqual(self.lib.rf_committed(), 0)

    def test_resume_after_restart_verifies_matching_files_without_rewrite(self):
        self.assertEqual(self.plan(), OK)
        self.upload(IMAGE2, self.target)
        old_session = self.session
        writes = self.lib.rf_count(OPEN_WRITE)
        self.lib.rf_reboot(0x50607080)
        self.assertEqual(self.lib.rf_verify(old_session, IMAGE2), SESSION)
        self.assertEqual(self.plan(), OK)
        self.assertEqual(self.lib.rf_verify(self.session, IMAGE2), OK)
        self.assertEqual(self.lib.rf_count(OPEN_WRITE), writes)
        self.assertEqual(self.lib.rf_select(self.session, 0), OK)

    def test_maximum_application_length_round_trips(self):
        target = image(MAX_IMAGE, reset=0x20010001)
        self.assertEqual(self.plan(target=target), OK)
        self.upload(IMAGE2, target)
        self.assertEqual(self.content(IMAGE2), target)
        self.assertEqual(self.lib.rf_file_allocation(IMAGE2), MAX_IMAGE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
