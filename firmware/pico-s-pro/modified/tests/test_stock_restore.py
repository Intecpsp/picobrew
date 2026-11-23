"""Exercise the production stock restore dispatcher and state through simulated power cuts."""
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from native_host import compiler, library_name, native_library, pic_flags, shared_flags

UID = "0123456789abcdef" * 2
ORIGINAL = ROOT.parent / "oem/device"
SLOTS = {name: (ORIGINAL / filename).read_bytes()
         for name, filename in [("application", "mcuimg2.bin"), ("recovery", "mcuimg1.bin")]}


class StockRestoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        source = ROOT / "platform/stock_restore"
        recovery = ROOT / "recovery"
        path = Path(cls.temp.name) / library_name("stock-restore")
        subprocess.run([compiler(), "-std=c11", *pic_flags(), *shared_flags(), "-I" + str(source),
                        "-I" + str(recovery / "include"), "-I" + str(recovery / "recovery"),
                        source / "stock.c", source / "http.c", recovery / "recovery/recovery.c",
                        recovery / "recovery/sha256.c", ROOT / "tests/stock_restore_fixture.c",
                        "-o", path], check=True)
        cls.context = native_library(path)
        cls.core = cls.context.__enter__()
        cls.core.fixture_response.restype = ctypes.c_char_p

    @classmethod
    def tearDownClass(cls):
        cls.context.__exit__(None, None, None)
        cls.temp.cleanup()

    def setUp(self):
        self.core.fixture_reset(*SLOTS.values())

    def req(self, op, status=200, auth=1, raw=None, **fields):
        body = raw if raw is not None else json.dumps({"device_uid": UID, **fields})
        code = self.core.fixture_request(b"GET" if op == "status" else b"POST",
                                         op.encode(), body.encode(), auth)
        result = json.loads(self.core.fixture_response().decode())
        self.assertEqual(code, status, result)
        return result

    def upload(self, target, corrupt=False):
        self.assertEqual(self.req("begin", target=target)["next_offset"], 0)
        data = SLOTS[target]
        if corrupt:
            data = bytes([data[0] ^ 1]) + data[1:]
        for i in range(0, len(data), 1024):
            part = data[i:i+1024]
            self.assertEqual(self.req("chunk", data=part.hex(), offset=i)["next_offset"], i+len(part))
        self.req("finish", 409 if corrupt else 200)

    def test_profile_auth_and_invalid_inputs_never_erase(self):
        self.req("begin", 401, target="application", auth=0)
        self.req("begin", 401, target="application", auth=-1)
        self.req("begin", 400, target="application", device_uid="another")
        self.req("begin", 400, raw='{"device_uid":"%s","target":"recovery","target":"application"}' % UID)
        self.req("begin", 400, target="loader")
        self.req("begin", 409, target="recovery")
        self.req("reboot", 409)
        for permitted, selector in [(0, 2), (1, 1), (1, 0), (1, -1)]:
            self.core.fixture_power_cycle()
            self.core.fixture_profile(permitted, selector)
            self.req("begin", 409, target="application")
        self.assertEqual(self.core.fixture_opens(0), 0)
        self.assertEqual(self.core.fixture_opens(1), 0)

    def test_wrong_bytes_offsets_and_write_failure(self):
        self.req("begin", target="application")
        self.req("chunk", 409, data="09", offset=1)
        self.req("chunk", 400, data="g1", offset=0)
        self.req("chunk", 400, data="01"*1025, offset=0)
        self.req("finish", 409)
        self.core.fixture_failure(1)
        self.req("chunk", 409, data="56", offset=0)
        self.req("commit", 409)
        self.core.fixture_failure(0)
        self.upload("application", corrupt=True)
        self.assertFalse(self.req("status")["app_verified"])
        self.req("begin", 409, target="recovery")
        self.req("commit", 409)
        self.assertEqual(self.core.fixture_cleanup(), 0)

    def test_power_loss_retry_and_readback(self):
        self.req("begin", target="application")
        self.req("chunk", data=SLOTS["application"][:1024].hex(), offset=0)
        self.core.fixture_power_cycle()
        self.assertFalse(self.req("status")["app_verified"])
        self.upload("application")
        self.req("begin", target="recovery")
        self.req("chunk", data="00", offset=0)
        self.core.fixture_power_cycle()
        state = self.req("status")
        self.assertTrue(state["app_verified"])
        self.assertFalse(state["recovery_verified"])
        self.assertEqual(self.req("begin", target="application")["next_offset"], len(SLOTS["application"]))
        self.upload("recovery", corrupt=True)
        self.req("commit", 409)
        self.upload("recovery")
        self.req("reboot", 409)
        self.core.fixture_failure(3)
        self.req("commit", 409)
        self.assertFalse(self.req("status")["boot_committed"])
        self.core.fixture_failure(4)
        self.req("commit", 409)
        self.req("reboot", 409)
        self.core.fixture_failure(0)
        self.req("commit")
        self.req("commit")
        self.assertEqual(self.core.fixture_cleanup(), 1)
        self.assertTrue(self.req("status")["boot_committed"])
        self.req("reboot")
        self.req("begin", 409, target="application")
        self.core.fixture_power_cycle()
        self.req("commit", 409)

    def test_pinned_oem_bytes_match_worker(self):
        native = (ROOT / "platform/stock_restore/native.cpp").read_text()
        for data in SLOTS.values():
            self.assertIn(hashlib.sha256(data).hexdigest(), native)


if __name__ == "__main__":
    unittest.main()
