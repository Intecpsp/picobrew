#!/usr/bin/env python3
"""Recovery browser workflows against the real C HTTP/core/readback/SHA code.

A loopback HTTP server delivers real gzip responses and dispatches JSON into
the portable fixture. No USB device, radio, physical flash or reset is used.
The separate HTTP/native suites cover production framing and device I/O.
"""
import copy
import gzip
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import struct
import threading
import unittest
from urllib.parse import urlsplit

from playwright.sync_api import expect, sync_playwright
import test_recovery_http as backend

ROOT = Path(__file__).resolve().parents[1]
TEST_KEY = "a" * 64  # Public test-only value; never included in firmware.
LOCK = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def handle_request(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        path = urlsplit(self.path).path
        body = json.loads(raw) if raw else None
        recorded = {"method": self.command, "path": path, "url": self.server.origin + self.path,
                    "headers": {name.lower(): value for name, value in self.headers.items()}, "body": body}
        encoding = None
        if path.startswith("/api/"):
            with LOCK:
                self.server.requests.append(recorded)
                code = self.server.c.recovery_http_call(
                    ["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"].index(self.command),
                    path.encode(), raw, len(raw), self.headers.get("Authorization") == "Bearer " + TEST_KEY, True)
                payload = bytes(self.server.c.recovery_http_response())
            if code <= 0:
                self.server.errors.append("C fixture did not handle " + self.command + " " + path)
                code, payload = 500, b'{"error":"fixture_unhandled"}'
            mime = "application/json"
        else:
            with LOCK:
                self.server.requests.append(recorded)
            assets = {"/": ("index.html", "text/html"), "/recovery.js": ("recovery.js", "text/javascript"),
                      "/style.css": ("style.css", "text/css")}
            if path not in assets:
                code, payload, mime = 404, b"", "text/plain"
            else:
                name, mime = assets[path]
                payload = gzip.compress((ROOT / "recovery/web" / name).read_bytes(), mtime=0)
                encoding, code = "gzip", 200
        self.send_response(code)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'")
        if encoding:
            self.send_header("Content-Encoding", encoding)
        self.end_headers()
        self.wfile.write(payload)

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = handle_request


def check_document(page, response, errors):
    """Fail once at startup, with useful evidence, instead of locator timeouts."""
    if (response is None or response.status != 200 or
            response.headers.get("content-encoding") != "gzip" or
            "text/html" not in response.headers.get("content-type", "") or
            page.locator("#connectForm #key").count() != 1 or errors):
        raise AssertionError("Recovery page did not load: " + json.dumps({
            "url": page.url, "status": response.status if response else None,
            "headers": response.headers if response else {}, "page_errors": errors,
            "dom_excerpt": page.content()[:900]}, ensure_ascii=True))


def package(web=True):
    image = struct.pack("<II", 0x2003F000, 0x20004009) + (bytes(range(256)) * 16)[:4088]
    report = {
        "profile": "standalone", "firmware_version": "1.0.0", "sdk_link_passed": True,
        "binary_sha256": hashlib.sha256(image).hexdigest(),
        "image_layout": {"passed": True, "binary_bytes": len(image), "load_address": "0x20004000",
                         "initial_stack_pointer": "0x2003f000", "reset_vector": "0x20004009"}}
    assets = []
    if web:
        manifest = []
        for name, code in zip(["index.html", "app.js", "api-explorer.js", "history.js", "style.css"],
                              ["i", "a", "e", "h", "s"]):
            data = gzip.compress((name + " fixture\n").encode() * 20, mtime=0)
            assets.append({"name": name + ".gz", "mimeType": "application/gzip", "buffer": data})
            manifest.append({"source": name, "payload": name + ".gz", "file_path": "/local/web/" + code + ".gz",
                             "content_encoding": "gzip", "sha256": hashlib.sha256(data).hexdigest(),
                             "length": len(data), "allocation_blocks": 1, "max_size": 3584})
        report["browser_storage"] = {"schema_version": 1, "block_size": 4096, "metadata_per_file": 512,
                                      "fail_safe": False, "assets": manifest}
    return image, report, assets


class RecoveryBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        backend.RecoveryHttp.setUpClass.__func__(cls)
        cls.server = cls.thread = cls.playwright = cls.browser = None
        try:
            cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            cls.server.c = cls.c
            cls.origin = cls.server.origin = "http://127.0.0.1:" + str(cls.server.server_port)
            cls.server.requests, cls.server.errors = [], []
            cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
            cls.thread.start()
            cls.playwright = sync_playwright().start()
            executable = os.environ.get("PICOBREW_CHROMIUM") or shutil.which("chromium")
            cls.browser = cls.playwright.chromium.launch(executable_path=executable, headless=True,
                                                         args=["--no-sandbox"])
            # Exercise Content-Encoding on the real HTTP wire, avoiding
            # precompressed synthetic Playwright route.fulfill responses.
            context = cls.browser.new_context()
            try:
                page = context.new_page()
                page.on("pageerror", lambda error: cls.server.errors.append(str(error)))
                response = page.goto(cls.origin, timeout=10000)
                check_document(page, response, cls.server.errors)
            finally:
                context.close()
        except Exception:
            cls.tearDownClass()
            raise
        cls.output = ROOT / "build/recovery-preview"
        cls.output.mkdir(parents=True, exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        if cls.browser:
            cls.browser.close()
        if cls.playwright:
            cls.playwright.stop()
        if cls.server:
            if cls.thread:
                cls.server.shutdown()
                cls.thread.join()
            cls.server.server_close()
        cls.temp.cleanup()

    def setUp(self):
        with LOCK:
            self.c.recovery_http_reset()
        self.requests, self.errors = [], []
        self.server.requests, self.server.errors = self.requests, self.errors
        self.context = self.browser.new_context(viewport={"width": 1280, "height": 1000})
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.page.set_default_timeout(8000)
        self.page.on("pageerror", lambda error: self.errors.append(str(error)))
        self.page.on("request", self.check_origin)
        response = self.page.goto(self.origin)
        check_document(self.page, response, self.errors)

    def tearDown(self):
        self.context.close()
        self.assertEqual(self.errors, [])

    def call(self, path="/api/v1/recovery", value=None, method="GET", auth=True):
        body = b"" if value is None else json.dumps(value).encode()
        with LOCK:
            code = self.c.recovery_http_call(["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"].index(method),
                                            path.encode(), body, len(body), auth, True)
            result = json.loads(self.c.recovery_http_response())
        return code, result

    def check_origin(self, request):
        url = urlsplit(request.url)
        if url.scheme + "://" + url.netloc != self.origin:
            self.errors.append("Unexpected external request: " + request.url)

    def posts(self, operation=None):
        with LOCK:
            return [request for request in self.requests if request["method"] == "POST" and
                    (operation is None or request["path"] == "/api/v1/recovery/" + operation)]

    def connect(self):
        self.page.locator("#key").fill(TEST_KEY)
        self.page.locator("#connect").click()
        expect(self.page.locator("#message")).to_contain_text("Connected.")
        expect(self.page.locator("#refresh")).to_be_enabled()

    def choose(self, data=None):
        image, report, assets = data or package()
        self.page.locator("#binary").set_input_files({"name": "pico.bin", "mimeType": "application/octet-stream", "buffer": image})
        self.page.locator("#report").set_input_files({"name": "firmware-report.json", "mimeType": "application/json",
                                                     "buffer": json.dumps(report).encode()})
        self.page.locator("#assets").set_input_files(assets)
        return image, report, assets

    def plan(self, data=None):
        selected = self.choose(data)
        self.page.locator("#plan").click()
        expect(self.page.locator("#upload")).to_be_enabled()
        return selected

    def upload(self):
        self.page.locator("#upload").click()
        expect(self.page.locator("#select")).to_be_enabled(timeout=15000)
        expect(self.page.locator("#message")).to_contain_text("Choose the next boot separately")

    def test_connection_file_selection_and_mobile_have_no_mutations_or_key_storage(self):
        self.connect()
        expect(self.page.locator('#slots tr[data-slot="3"]')).to_contain_text("Unavailable (layout migration needed)")
        self.assertTrue(self.page.locator('#slot option[value="3"]').is_disabled())
        self.choose()
        self.page.locator("#slot").select_option("2")
        self.assertEqual(self.posts(), [])
        self.assertEqual(self.c.recovery_http_write_opens(), 0)
        expect(self.page.locator("#key")).to_have_value("")
        self.assertNotIn(TEST_KEY, self.page.content())
        self.assertEqual(self.page.evaluate("[localStorage.length,sessionStorage.length,document.cookie]"), [0, 0, ""])
        for request in self.requests:
            self.assertNotIn(TEST_KEY, request["url"])
            self.assertNotIn(TEST_KEY, json.dumps(request["body"]))
            if request["path"].startswith("/api/"):
                self.assertEqual(request["headers"]["authorization"], "Bearer " + TEST_KEY)
        self.page.screenshot(path=str(self.output / "recovery-desktop.png"), full_page=True)
        self.page.set_viewport_size({"width": 390, "height": 844})
        self.assertFalse(self.page.evaluate("document.documentElement.scrollWidth > innerWidth"))
        self.page.screenshot(path=str(self.output / "recovery-mobile.png"), full_page=True)
        self.page.locator("#disconnect").click()
        expect(self.page.locator("#boot")).to_have_text("Disconnected.")
        expect(self.page.locator("#plan")).to_be_disabled()
        self.assertEqual(self.posts(), [])

    def test_full_package_web_first_real_readback_and_only_explicit_trial_boot(self):
        self.connect()
        image, report, assets = self.plan()
        self.assertEqual(self.c.recovery_http_write_opens(), 0)
        planned = self.posts("plan")[0]["body"]
        self.assertEqual(planned["sha256"], report["binary_sha256"])
        self.assertEqual(planned["size"], len(image))
        self.assertEqual(planned["index_sha256"], hashlib.sha256(assets[0]["buffer"]).hexdigest())
        self.upload()
        self.assertEqual([entry["body"]["target"] for entry in self.posts("begin")], [1, 2, 3, 4, 5, 0])
        for request in self.posts("chunk"):
            self.assertLessEqual(len(bytes.fromhex(request["body"]["data"])), 1024)
        _, inventory = self.call()
        self.assertEqual(inventory["plan"]["verified_mask"], 63)
        self.assertTrue(inventory["slots"][0]["vectors_valid"])
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)
        self.assertEqual(self.posts("select"), [])
        dialogs = []

        def dismiss(dialog):
            dialogs.append(dialog.message)
            dialog.dismiss()

        self.page.once("dialog", dismiss)
        self.page.locator("#select").click()
        self.assertIn("USB boot-metadata programming", dialogs[0])
        self.assertIn("restart now", dialogs[0])
        self.assertEqual(self.posts("select"), [])
        self.page.once("dialog", lambda dialog: dialog.accept())
        self.page.locator("#trial").click()
        expect(self.page.locator("#message")).to_have_text("Boot selection verified. The Pico is restarting.")
        expect(self.page.locator("#select")).to_be_disabled()
        self.assertEqual(len(self.posts("select")), 1)
        self.assertTrue(self.posts("select")[0]["body"]["trial"])
        self.assertEqual(bytes(self.c.recovery_http_boot()[:8]), bytes.fromhex("00 3d 00 20 65 87 78 56"))
        # No inventory request is sent after select: the real device restarts.
        self.assertEqual([entry for entry in self.requests if entry["path"].startswith("/api/")][-1]["path"],
                         "/api/v1/recovery/select")

    def test_new_plan_can_verify_installed_files_without_rewrite_and_select_default(self):
        self.connect()
        selected = self.plan(package(web=False))
        self.upload()
        write_count = self.c.recovery_http_write_opens()
        first_id = self.posts("begin")[0]["body"]["id"]
        self.page.locator("#disconnect").click()
        self.connect()
        self.plan(selected)
        self.page.locator("#verify").click()
        expect(self.page.locator("#select")).to_be_enabled()
        self.assertEqual(self.c.recovery_http_write_opens(), write_count)
        self.assertNotEqual(self.posts("verify")[0]["body"]["id"], first_id)
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)
        self.page.once("dialog", lambda dialog: dialog.accept())
        self.page.locator("#select").click()
        expect(self.page.locator("#message")).to_contain_text("restarting")
        self.assertEqual(bytes(self.c.recovery_http_boot()[:8]), bytes.fromhex("01 3d 00 20 ba dc cd ab"))

    def test_bad_package_reports_are_rejected_before_plan_or_write(self):
        self.connect()
        image, original, assets = package()
        variants = []
        for field, value in [("profile", "recovery"), ("sdk_link_passed", False),
                             ("binary_sha256", "g" * 64), ("firmware_version", "../bad")]:
            bad = copy.deepcopy(original)
            bad[field] = value
            variants.append((image, bad, assets))
        bad = copy.deepcopy(original)
        bad["image_layout"]["binary_bytes"] += 1
        variants.append((image, bad, assets))
        bad = copy.deepcopy(original)
        bad["browser_storage"]["assets"][0]["file_path"] = "/sys/mcuimg1.bin"
        variants.append((image, bad, assets))
        variants.append((image, original, assets[:-1]))
        variants.append((b"\0" * len(image), original, assets))
        for selected in variants:
            self.choose(selected)
            self.page.locator("#plan").click()
            expect(self.page.locator("#message")).to_have_class("error")
            expect(self.page.locator("#plan")).to_be_enabled()
            self.assertEqual(self.posts(), [])
        self.assertEqual(self.c.recovery_http_write_opens(), 0)
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)

    def test_mismatched_file_hash_fails_real_readback_without_retry_or_boot(self):
        self.connect()
        image, report, assets = package()
        changed = bytearray(image)
        changed[100] ^= 1  # Vectors and length remain valid; declared digest does not.
        self.plan((bytes(changed), report, assets))
        self.page.locator("#upload").click()
        expect(self.page.locator("#message")).to_contain_text("checksum", timeout=15000)
        expect(self.page.locator("#refresh")).to_be_enabled()
        expect(self.page.locator("#select")).to_be_disabled()
        self.assertEqual([entry["body"]["target"] for entry in self.posts("begin")], [1, 2, 3, 4, 5, 0])
        self.assertEqual(len(self.posts("finish")), 6)
        self.assertEqual(self.posts("select"), [])
        self.assertEqual(self.posts("abort"), [])
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)
        _, inventory = self.call()
        self.assertTrue(inventory["boot"]["recovery_selected"])
        self.assertEqual(inventory["plan"]["verified_mask"], 62)

    def test_another_tab_replacing_plan_invalidates_old_upload_before_writes(self):
        self.connect()
        self.plan(package(web=False))
        accepted = self.posts("plan")[0]["body"]
        code, _ = self.call("/api/v1/recovery/plan", accepted, "POST")
        self.assertEqual(code, 200)
        self.page.locator("#upload").click()
        expect(self.page.locator("#message")).to_contain_text("session")
        expect(self.page.locator("#upload")).to_be_disabled()
        self.assertEqual(self.c.recovery_http_write_opens(), 0)
        self.assertEqual(self.c.recovery_http_boot_writes(), 0)
        self.assertEqual(self.posts("select"), [])

    def test_disconnect_during_local_report_read_cannot_mutate_new_connection(self):
        self.connect()
        self.choose(package(web=False))
        self.page.evaluate("""() => {
          const original=Blob.prototype.text;
          const buffer=Blob.prototype.arrayBuffer;
          Blob.prototype.arrayBuffer=async function(){
            const result=await buffer.call(this);window.oldFileReadFinished=true;return result;
          };
          Blob.prototype.text=function(){
            if(this instanceof File && this.name==='firmware-report.json')
              return new Promise(resolve=>{window.releaseReport=async()=>resolve(await original.call(this));});
            return original.call(this);
          };
        }""")
        self.page.locator("#plan").click()
        self.page.wait_for_function("typeof releaseReport === 'function'")
        self.page.locator("#disconnect").click()
        self.connect()
        self.page.evaluate("releaseReport()")
        self.page.wait_for_function("window.oldFileReadFinished === true")
        self.page.locator("#refresh").click()
        expect(self.page.locator("#message")).to_have_text("Inventory refreshed.")
        expect(self.page.locator("#plan")).to_be_enabled()
        self.assertEqual(self.posts(), [])
        self.assertEqual(self.c.recovery_http_write_opens(), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
