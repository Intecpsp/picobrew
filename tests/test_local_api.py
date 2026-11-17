#!/usr/bin/env python3
"""Host integration tests for the actual standalone API and persistent store.

This exercises JSON with the same shared request/response arena as the native
HTTP server. Hardware and flash transport are simulated; no device is flashed.
"""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TEMP = tempfile.TemporaryDirectory(prefix="picobrew-local-api-")
LIBRARY = Path(TEMP.name) / "api.so"
SOURCES = [ROOT / "tests/standalone/local_api_host.c", ROOT / "src/local_api.c", ROOT / "src/local_store.c"]
subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", "-O1", "-g",
                "-I" + str(ROOT / "include"), *map(str, SOURCES), "-o", str(LIBRARY)], check=True)
LIB = ctypes.CDLL(str(LIBRARY))
LIB.test_request.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t,
                             ctypes.c_void_p, ctypes.c_size_t]
LIB.test_session.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
LIB.test_corrupt.argtypes = [ctypes.c_char_p, ctypes.c_uint]
LIB.test_load_recipe.argtypes = [ctypes.c_char_p]
LIB.test_event.argtypes = [ctypes.c_char_p, ctypes.c_char_p]


def request(method, path, body=None, raw=None):
    payload = raw if raw is not None else (json.dumps(body, separators=(",", ":")).encode() if body is not None else b"")
    output = ctypes.create_string_buffer(4096)
    code = LIB.test_request(method.encode(), path.encode(), payload, len(payload), output, len(output))
    parsed = json.loads(output.value) if output.value else None
    return code, parsed


def recipe(name="House ale", count=1):
    return {"name": name, "steps": [{"name": f"Mash {i}", "location": i % 6 + 1,
            "target_f": 152, "minutes": 60, "drain_minutes": 2} for i in range(count)]}


class LocalApi(unittest.TestCase):
    def setUp(self):
        LIB.test_reset(1)

    def test_discovery_health_status_and_screen(self):
        code, data = request("GET", "/api/v1")
        self.assertEqual(code, 200)
        self.assertIn("encoder", data["commands"])
        self.assertFalse(data["dedicated_pause_resume"])
        self.assertEqual(request("GET", "/api/v1/health")[1]["uptime_ms"], 123456)
        status = request("GET", "/api/v1/status")[1]
        self.assertEqual(status["wort_f"], 72.4)
        self.assertTrue(status["filter_inserted"])
        self.assertEqual(status["encoder"]["max"], 5)
        self.assertEqual(status["stop_reason"], "none")
        screen = request("GET", "/api/v1/display")[1]
        self.assertEqual(bytes.fromhex(screen["data"]), bytes(range(256)) * 4)
        self.assertEqual((screen["width"], screen["height"]), (128, 64))
        LIB.test_ready(0)
        self.assertIsNone(request("GET", "/api/v1/status")[1]["wort_f"])

    def test_recipe_crud_reboot_and_existing_ui_bridge(self):
        self.assertEqual(request("GET", "/api/v1/recipes"), (200, {"recipes": [], "capacity": 16}))
        original = recipe(count=15)
        code, saved = request("PUT", "/api/v1/recipes/house-ale", original)
        self.assertEqual(code, 201)
        self.assertEqual(saved, dict(id="house-ale", **original))
        self.assertEqual(LIB.test_recipe_list(), 1)
        self.assertEqual(LIB.test_load_recipe(b"house-ale"), 1)
        self.assertEqual(request("GET", "/api/v1/status")[1]["recipe_name"], "House ale")
        LIB.test_reset(0)
        self.assertEqual(request("GET", "/api/v1/recipes/house-ale"), (200, saved))
        modified = recipe('Quoted "ale" \\ test')
        self.assertEqual(request("PUT", "/api/v1/recipes/house-ale", modified)[0], 200)
        self.assertEqual(request("GET", "/api/v1/recipes")[1]["recipes"][0]["name"], modified["name"])
        self.assertEqual(request("DELETE", "/api/v1/recipes/house-ale"), (204, None))
        LIB.test_reset(0)
        self.assertEqual(request("GET", "/api/v1/recipes/house-ale")[0], 404)

    def test_recipe_capacity_and_slot_reuse(self):
        for index in range(16):
            self.assertEqual(request("PUT", f"/api/v1/recipes/r{index}", recipe())[0], 201)
        self.assertEqual(len(request("GET", "/api/v1/recipes")[1]["recipes"]), 16)
        self.assertEqual(request("PUT", "/api/v1/recipes/overflow", recipe())[0], 507)
        self.assertEqual(request("DELETE", "/api/v1/recipes/r7")[0], 204)
        self.assertEqual(request("PUT", "/api/v1/recipes/reused", recipe())[0], 201)
        LIB.test_reset(0)
        self.assertEqual(request("GET", "/api/v1/recipes/r7")[0], 404)
        self.assertEqual(request("GET", "/api/v1/recipes/reused")[0], 200)

    def test_invalid_recipe_and_json_never_write(self):
        invalid = [b"", b"{}", b"null", b"[]", b"{\"name\":\"x\",\"steps\":[]}",
            b'{"name":"x","name":"y","steps":[]}', b'{"name":"x","steps":[]}trailing',
            b'{"name":"x\\u0000","steps":[]}', b'{"name":"x\\ud800","steps":[]}',
            b'{"name":"x\\n","steps":[]}', b'{"name":"x","steps":[],}', b'{"x":true}',
            b'{"name":"x","steps":[],"__proto__":{}}']
        for payload in invalid:
            with self.subTest(raw=payload):
                self.assertEqual(request("PUT", "/api/v1/recipes/x", raw=payload)[0], 400)
        for path in ["../x", "a/b", "%2e%2e", "x?x", "x" * 21, ""]:
            self.assertEqual(request("PUT", "/api/v1/recipes/" + path, recipe())[0], 400)
        for field, value in [("location", 0), ("location", 7), ("target_f", 59), ("target_f", 213),
                              ("minutes", 0), ("minutes", 600), ("minutes", 1.5), ("minutes", True),
                              ("drain_minutes", -1), ("drain_minutes", 121), ("name", "x" * 22)]:
            document = recipe(); document["steps"][0][field] = value
            with self.subTest(field=field, value=value):
                self.assertEqual(request("PUT", "/api/v1/recipes/x", document)[0], 400)
        for document in [recipe(count=0), recipe(count=16), recipe("x" * 32),
                         dict(recipe(), id="another-id"), dict(recipe(), unknown=1)]:
            self.assertEqual(request("PUT", "/api/v1/recipes/x", document)[0], 400)
        self.assertEqual(LIB.test_file_count(), 0)

    def test_every_truncated_write_preserves_committed_recipe(self):
        # Every truncation point in a recipe record and catalog commit, followed
        # by a simulated reboot. The previous committed image must remain usable.
        for write_index, size in [(0, 489), (1, 432)]:
            for truncated in range(size):
                LIB.test_reset(1)
                self.assertEqual(request("PUT", "/api/v1/recipes/test", recipe("Before"))[0], 201)
                LIB.test_write_failure(write_index, truncated)
                self.assertEqual(request("PUT", "/api/v1/recipes/test", recipe("After"))[0], 503)
                LIB.test_reset(0)
                self.assertEqual(request("GET", "/api/v1/recipes/test")[1]["name"], "Before",
                                 (write_index, truncated))

    def test_storage_read_failures_do_not_initialize_empty_catalog(self):
        self.assertEqual(request("PUT", "/api/v1/recipes/test", recipe())[0], 201)
        count = LIB.test_file_count()
        LIB.test_stat_failure(0)
        self.assertEqual(request("PUT", "/api/v1/recipes/another", recipe())[0], 503)
        self.assertEqual(LIB.test_file_count(), count)
        LIB.test_stat_failure(-1); LIB.test_read_failure(0)
        self.assertEqual(request("PUT", "/api/v1/recipes/another", recipe())[0], 503)
        self.assertEqual(LIB.test_file_count(), count)
        LIB.test_read_failure(-1)
        self.assertEqual(request("GET", "/api/v1/recipes/test")[0], 200)
        self.assertEqual(LIB.test_corrupt(b"/local/index.a", 20), 1)
        self.assertEqual(request("PUT", "/api/v1/recipes/another", recipe())[1]["error"], "storage_corrupt")
        self.assertEqual(LIB.test_file_count(), count)

    def test_partial_overwrite_with_retained_flash_tail_is_commit_uncertain(self):
        # Existing SLFS files may retain their tail after a short write. A
        # partial catalog prefix can then be a valid full new commit. A 503
        # must permit either the old or the fully verified new recipe, never
        # malformed/mixed records. This is why clients must GET before retry.
        for write_index, size in [(0, 489), (1, 432)]:
            for truncated in range(size):
                LIB.test_reset(1)
                self.assertEqual(request("PUT", "/api/v1/recipes/test", recipe("Initial"))[0], 201)
                self.assertEqual(request("PUT", "/api/v1/recipes/test", recipe("Before"))[0], 200)
                LIB.test_retain_tail(1)
                LIB.test_write_failure(write_index, truncated)
                self.assertEqual(request("PUT", "/api/v1/recipes/test", recipe("After"))[0], 503)
                LIB.test_reset(0)
                code, saved = request("GET", "/api/v1/recipes/test")
                self.assertEqual(code, 200, (write_index, truncated))
                allowed = [dict(id="test", **recipe("Before"))]
                if write_index == 1: allowed.append(dict(id="test", **recipe("After")))
                self.assertIn(saved, allowed, (write_index, truncated))

    def test_busy_mutations_and_validated_command_dispatch(self):
        self.assertEqual(request("POST", "/api/v1/commands", {"command": "brew", "recipe_id": "missing"})[0], 404)
        self.assertEqual(LIB.test_control_calls(), 0)
        self.assertEqual(request("PUT", "/api/v1/recipes/test", recipe())[0], 201)
        self.assertEqual(request("POST", "/api/v1/commands", {"command": "brew", "recipe_id": "test"})[0], 202)
        self.assertEqual(LIB.test_control_calls(), 1)
        for method, path, body in [("PUT", "/api/v1/recipes/test", recipe()),
                                    ("DELETE", "/api/v1/recipes/test", None),
                                    ("PATCH", "/api/v1/settings", {"language": "es"})]:
            self.assertEqual(request(method, path, body)[0], 409)
        self.assertEqual(request("POST", "/api/v1/commands", {"command": "stop"})[0], 202)
        self.assertTrue(request("GET", "/api/v1/status")[1]["stopped"])
        self.assertEqual(request("GET", "/api/v1/status")[1]["stop_reason"], "requested")
        self.assertEqual(request("POST", "/api/v1/commands", {"command": "clear-stop"})[0], 409)
        LIB.test_busy(0)
        self.assertEqual(request("POST", "/api/v1/commands", {"command": "rinse"})[0], 409)
        self.assertEqual(request("POST", "/api/v1/commands", {"command": "clear-stop"})[0], 202)
        self.assertEqual(request("POST", "/api/v1/commands", {"command": "encoder", "delta": -3, "click": True})[0], 202)
        self.assertEqual((LIB.test_encoder_calls(), LIB.test_last_delta(), LIB.test_last_click()), (1, -3, 1))
        for command in [{"command": "heater", "on": True}, {"command": "encoder", "delta": 101},
                        {"command": "encoder", "delta": 0}, {"command": "rinse", "delta": 1},
                        {"command": "brew"}, {"command": "pause"}, {"command": "encoder", "click": 1}]:
            self.assertEqual(request("POST", "/api/v1/commands", command)[0], 400)

    def test_settings_and_session_sequences_survive_reboot(self):
        expected = {"temperature_unit": "C", "language": "fr", "detailed_status": True}
        self.assertEqual(request("PATCH", "/api/v1/settings", expected), (200, expected))
        one = ctypes.create_string_buffer(32); two = ctypes.create_string_buffer(32)
        self.assertEqual(LIB.test_session(one, len(one)), 1)
        LIB.test_reset(0)
        self.assertEqual(LIB.test_load_settings(), 1)
        self.assertEqual(request("GET", "/api/v1/settings"), (200, expected))
        self.assertEqual(LIB.test_session(two, len(two)), 1)
        self.assertEqual(one.value, b"local-01234567-00000001")
        self.assertEqual(two.value, b"local-01234567-00000002")
        for settings in [{}, {"wifi_password": "secret"}, {"language": "de"},
                         {"temperature_unit": "c"}, {"detailed_status": 1}]:
            self.assertEqual(request("PATCH", "/api/v1/settings", settings)[0], 400)
        LIB.test_write_failure(0, 8)
        self.assertEqual(request("PATCH", "/api/v1/settings", {"language": "en"})[0], 503)
        LIB.test_reset(0); self.assertEqual(LIB.test_load_settings(), 1)
        self.assertEqual(request("GET", "/api/v1/settings"), (200, expected))

    def test_event_ring_is_bounded_and_honest_about_persistence(self):
        for i in range(20): LIB.test_event(b"sample", str(i).encode())
        data = request("GET", "/api/v1/events")[1]
        self.assertTrue(data["volatile"])
        self.assertEqual([event["detail"] for event in data["events"]], list(map(str, range(12, 20))))
        self.assertEqual(data["next_sequence"], 21)
        LIB.test_reset(0)
        self.assertEqual(request("GET", "/api/v1/events")[1]["events"], [])


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(LocalApi)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful(): raise SystemExit(1)
    executable = Path(TEMP.name) / "api-sanitized"
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DLOCAL_API_SELFTEST_MAIN",
                    "-I" + str(ROOT / "include"), *map(str, SOURCES), "-o", str(executable)], check=True)
    # This target does not allocate. LeakSanitizer cannot inspect processes in
    # ptrace-based execution environments; ASan and UBSan remain fully enabled.
    environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
    subprocess.run([str(executable)], check=True, env=environment)
