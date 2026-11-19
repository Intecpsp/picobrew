#!/usr/bin/env python3
"""Exercise authenticated recovery JSON dispatch with real core/readback/SHA.

No sockets or device access. Bodies alias response memory exactly as in the
firmware. The existing HTTP transport suite separately covers byte framing,
authorization headers, partial socket I/O and deadlines.
"""
import ctypes
import hashlib
import json
import os
import random
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RecoveryHttp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="pico-recovery-http-")
        library = Path(cls.temp.name) / "recovery.so"
        subprocess.run(["gcc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                        "-I" + str(ROOT / "include"), "-I" + str(ROOT / "recovery"),
                        *[str(ROOT / p) for p in ["recovery/http.c", "recovery/recovery.c", "recovery/sha256.c",
                                                 "tests/recovery_http_host.c"]], "-o", str(library)], check=True)
        cls.c = ctypes.CDLL(str(library))
        cls.c.recovery_http_call.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
                                           ctypes.c_size_t, ctypes.c_int, ctypes.c_int]
        cls.c.recovery_http_response.restype = ctypes.c_char_p
        cls.c.recovery_http_boot.restype = ctypes.POINTER(ctypes.c_ubyte)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.c.recovery_http_reset()
        self.image = struct.pack("<II", 0x2003F000, 0x20004009) + bytes(range(256)) * 4
        self.plan = dict(slot=2, size=len(self.image), sha256=hashlib.sha256(self.image).hexdigest(),
                         version="1.0.0", web=False)

    def request(self, operation="", value=None, *, raw=None, method=None, auth=True, configured=True):
        path = "/api/v1/recovery" + ("/" + operation if operation else "")
        if method is None:
            method = 0 if not operation else 1
        body = raw if raw is not None else json.dumps(value).encode() if value is not None else b""
        status = self.c.recovery_http_call(method, path.encode(), body, len(body), auth, configured)
        self.assertGreater(status, 0)
        return status, json.loads(self.c.recovery_http_response())

    def create_plan(self, **changes):
        status, reply = self.request("plan", {**self.plan, **changes})
        self.assertEqual(status, 200, reply)
        return reply["id"]

    def upload(self, session):
        self.assertEqual(self.request("begin", dict(id=session, target=0))[0], 200)
        for offset in range(0, len(self.image), 1024):
            status, reply = self.request("chunk", dict(id=session, offset=offset,
                                                       data=self.image[offset:offset + 1024].hex()))
            self.assertEqual(status, 200, reply)
        self.assertEqual(self.request("finish", dict(id=session))[0], 200)

    def test_inventory_health_auth_and_method_boundaries(self):
        status, reply = self.request()
        self.assertEqual(status, 200)
        self.assertEqual(reply["profile"], "recovery")
        self.assertTrue(reply["slots"][0]["present"])
        self.assertFalse(reply["slots"][1]["present"])
        self.assertIsNone(reply["slots"][0]["vectors_valid"])
        self.assertEqual(self.request(auth=False)[0], 401)
        self.assertEqual(self.request(configured=False)[0], 503)
        self.assertEqual(self.request("plan", self.plan, auth=False)[0], 401)
        self.assertEqual(self.request("plan", self.plan, method=2)[0], 405)
        self.assertEqual(self.request("plan", self.plan, method=0)[0], 405)
        self.assertEqual(self.request("unknown", {})[0], 404)
        status = self.c.recovery_http_call(0, b"/api/v1/health", b"", 0, 0, 1)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(self.c.recovery_http_response())["mode"], "recovery")
        self.assertEqual(self.c.recovery_http_write_opens(), 0)

    def test_aliasing_full_upload_readback_and_explicit_default_selection(self):
        session = self.create_plan()
        self.upload(session)
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)
        status, inventory = self.request()
        self.assertEqual(status, 200)
        self.assertEqual(inventory["plan"]["verified_mask"], 1)
        self.assertTrue(inventory["slots"][0]["vectors_valid"])
        status, reply = self.request("select", dict(id=session, trial=False))
        self.assertEqual(status, 200, reply)
        self.assertTrue(reply["boot_committed"])
        self.assertEqual(bytes(self.c.recovery_http_boot()[:8]), bytes.fromhex("01 3d 00 20 ba dc cd ab"))

    def test_trial_keeps_recovery_as_return_destination(self):
        session = self.create_plan()
        self.upload(session)
        self.assertEqual(self.request("select", dict(id=session, trial=True))[0], 200)
        self.assertEqual(bytes(self.c.recovery_http_boot()[:8]), bytes.fromhex("00 3d 00 20 65 87 78 56"))

    def test_stale_sessions_and_unverified_selection_cannot_write(self):
        old = self.create_plan()
        current = self.create_plan()
        self.assertNotEqual(old, current)
        for operation, params in [("begin", dict(id=old, target=0)), ("verify", dict(id=old, target=0)),
                                  ("select", dict(id=old, trial=False)), ("select", dict(id=current, trial=False))]:
            self.assertEqual(self.request(operation, params)[0], 409)
        self.assertEqual(self.c.recovery_http_write_opens(), 0)
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)

    def test_invalid_header_and_hex_fail_before_destructive_open(self):
        session = self.create_plan()
        self.assertEqual(self.request("begin", dict(id=session, target=0))[0], 200)
        for data in ["", "0", "gg", "AA", "00" * 1025, "00" * 8]:
            status, _ = self.request("chunk", dict(id=session, offset=0, data=data))
            self.assertIn(status, (400, 409))
        self.assertEqual(self.c.recovery_http_write_opens(), 0)

    def test_absent_image3_and_changed_boot_record_are_rejected(self):
        self.assertEqual(self.request("plan", {**self.plan, "slot": 3})[0], 409)
        self.c.recovery_http_boot_selector(1)
        self.assertEqual(self.request("plan", self.plan)[0], 409)
        self.assertEqual(self.c.recovery_http_write_opens(), 0)

    def test_strict_json_does_not_mutate_on_unknown_duplicate_or_numeric_input(self):
        malformed = [b"", b"[]", b"null", b"{}x", b'{"slot":2,}', b'{"slot":02}', b'{"slot":-2}',
                     b'{"slot":2.0}', b'{"slot":2e0}', b'{"slot":4294967296}',
                     b'{"slot":2,"slot":2}', b'{"web":truex}', b'{"version":"a\\u0000"}',
                     b"{" * 4096, b'{"slot":2}\x00']
        for raw in malformed:
            self.assertEqual(self.request("plan", raw=raw)[0], 400, raw[:100])
        for changed in [{"path": "/sys/mcuimg1.bin"}, {"size": True}, {"version": "\""},
                        {"sha256": "00" * 31}, {"slot": 1}, {"web": 1}]:
            self.assertEqual(self.request("plan", {**self.plan, **changed})[0], 400, changed)
        self.assertEqual(self.c.recovery_http_write_opens(), 0)
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)

    def test_all_truncated_plan_and_chunk_bodies_fail_before_mutation(self):
        raw = json.dumps(self.plan).encode()
        for length in range(len(raw)):
            self.assertEqual(self.request("plan", raw=raw[:length])[0], 400)
        session = self.create_plan()
        self.assertEqual(self.request("begin", dict(id=session, target=0))[0], 200)
        raw = json.dumps(dict(id=session, offset=0, data=self.image[:1024].hex())).encode()
        for length in range(len(raw)):
            self.assertEqual(self.request("chunk", raw=raw[:length])[0], 400)
        self.assertEqual(self.c.recovery_http_write_opens(), 0)

    def test_random_ascii_objects_never_open_files(self):
        randomizer = random.Random(3200)
        alphabet = b'{}[],:"0123456789abcdeftrunl \\x00'
        for _ in range(1500):
            raw = bytes(randomizer.choice(alphabet) for _ in range(randomizer.randrange(180)))
            status, _ = self.request("plan", raw=raw)
            self.assertEqual(status, 400)
        self.assertEqual(self.c.recovery_http_write_opens(), 0)

    def test_memory_safety_under_arbitrary_request_bytes(self):
        executable = Path(self.temp.name) / "recovery-http-asan"
        subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DRECOVERY_HTTP_FUZZ_MAIN",
                        "-I" + str(ROOT / "include"), "-I" + str(ROOT / "recovery"),
                        *[str(ROOT / p) for p in ["recovery/http.c", "recovery/recovery.c", "recovery/sha256.c",
                                                 "tests/recovery_http_host.c"]], "-o", str(executable)], check=True)
        environment = dict(os.environ)
        environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        subprocess.run([str(executable)], check=True, env=environment)


if __name__ == "__main__":
    unittest.main(verbosity=2)
