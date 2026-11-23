from contextlib import nullcontext
import io
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import usb_bridge as bridge


class WorkerTests(unittest.TestCase):
    def test_another_hub_worker_cannot_open_the_same_usb_service(self):
        import usb_avr
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "hub"
            directory = data / "usb-jobs"
            bridge.private_files.ensure_private_directory(directory, parents=True, exist_ok=True)
            target = {"port": "test", "label": "Test", "models": ["pico-s"]}
            req = {"action": "backup", "model": "pico-s", "port": "test", "data_dir": str(data), "job_id": "1"*32}
            with usb_avr.service_lock(directory):
                with self.assertRaisesRegex(bridge.UserError,"Another USB operation"):
                    bridge.run(req, io.BytesIO(), self.fail, discover=lambda: {"test": target})
            self.assertFalse((directory / req["job_id"]).exists())

    def test_list_hides_paths_and_serials_and_rejects_disconnected_target(self):
        serial = SimpleNamespace(
            vid=0x20A0,
            pid=0x421E,
            serial_number="private-serial",
            location="private-location",
            device="/dev/ttyUSB0",
        )
        entries = bridge.serial_devices([serial])
        events = []
        bridge.run({"action": "list"}, None, events.append, lambda: entries)
        self.assertEqual(events[0]["type"], "devices")
        device = events[0]["devices"][0]
        self.assertEqual(device["models"], ["zymatic"])
        self.assertEqual(set(device), {"label", "port", "models"})
        self.assertNotIn("private-", json.dumps(device))
        self.assertNotIn("/dev/", json.dumps(device))
        with self.assertRaisesRegex(bridge.UserError, "unavailable"):
            bridge.run(
                {"action": "backup", "model": "zymatic", "port": device["port"]},
                None,
                events.append,
                lambda: {},
            )

    def test_confirmations_cannot_be_replayed_and_keys_stay_private(self):
        import usb_avr
        import usb_stock

        entries = {
            "serial-t": {
                "port": "serial-t",
                "label": "Zymatic",
                "models": ["zymatic"],
                "path": "simulated",
            }
        }
        with tempfile.TemporaryDirectory() as tmp:
            request = {
                "action": "install",
                "data_dir": str(Path(tmp).resolve()),
                "job_id": "e" * 32,
                "model": "zymatic",
                "port": "serial-t",
            }
            events = []
            with patch.object(
                usb_avr, "usb_identity", return_value=("simulated", "one", "location")
            ), patch.object(usb_avr, "service") as service:
                with self.assertRaisesRegex(bridge.UserError, "canceled"):
                    bridge.run(
                        request, io.BytesIO(b'{"confirm":"old"}\n'), events.append, lambda: entries
                    )
                service.assert_not_called()
                events.clear()

                def complete(data, port, action, *, confirm, key_file, hub_host, expected_uid):
                    self.assertEqual(confirm("Type INSTALL ZYMATIC: "), "INSTALL ZYMATIC")
                    self.assertEqual(confirm("Type WRITE 1.2.0: "), "WRITE 1.2.0")
                    self.assertNotEqual(key_file("b" * 32), key_file("c" * 32))
                    usb_stock.save_private(key_file("b" * 32), b"a" * 64)
                    return "b" * 32

                service.side_effect = complete

                class Confirmations:
                    def readline(self, n):
                        return (json.dumps({"confirm": events[-1]["id"]}) + "\n").encode()

                bridge.run(request, Confirmations(), events.append, lambda: entries)
                folder = Path(tmp) / "usb-jobs" / request["job_id"]
                self.assertEqual((folder / "device-key").read_bytes(), b"a" * 64)
                self.assertEqual((folder / "device-uid").read_bytes(), b"b" * 32)
                self.assertNotIn("a" * 64, json.dumps(events))
                self.assertNotIn(str(Path(tmp)), json.dumps(events))
                self.assertEqual(len({e["id"] for e in events if e["type"] == "confirm"}), 3)

    def test_esp_pairing_receipt_uses_the_observed_unit_and_survives_retry(self):
        import esp_usb

        identity = esp_usb.Identity("ESP32", "aabbccddeeff", 0x1640C8, 4 * esp_usb.MIB)
        device = SimpleNamespace(identity=identity)
        entries = {
            "serial-t": {
                "port": "serial-t",
                "label": "ESP",
                "models": ["picostill"],
                "path": "simulated",
            }
        }
        events = []

        class Confirmations:
            def readline(self, n):
                return (json.dumps({"confirm": events[-1]["id"]}) + "\n").encode()

        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp).resolve()
            request = {
                "action": "install",
                "model": "picostill",
                "port": "serial-t",
                "data_dir": str(data),
                "job_id": "1" * 32,
            }
            with patch.object(
                esp_usb, "open_device", side_effect=lambda *args: nullcontext(device)
            ), patch.object(esp_usb, "operation") as operation:
                with self.assertRaisesRegex(bridge.UserError, "registered machine"):
                    bridge.run(
                        dict(request, expected_uid="001122334455"),
                        Confirmations(),
                        events.append,
                        lambda: entries,
                    )
                operation.assert_not_called()
                operation.side_effect = ValueError("fixture readback rejection")
                with self.assertRaisesRegex(ValueError, "readback"):
                    bridge.run(request, Confirmations(), events.append, lambda: entries)
                self.assertFalse((data / "usb-jobs" / request["job_id"] / "device-key").exists())
                original_key = operation.call_args.kwargs["api_key"]
                operation.side_effect = None
                request["job_id"] = "2" * 32
                bridge.run(
                    dict(request, expected_uid=identity.mac),
                    Confirmations(),
                    events.append,
                    lambda: entries,
                )
                self.assertEqual(operation.call_args.kwargs["api_key"], original_key)
                folder = data / "usb-jobs" / request["job_id"]
                self.assertEqual((folder / "device-uid").read_text(), identity.mac)
                self.assertEqual((folder / "device-key").read_text(), original_key)
                self.assertNotIn(original_key, json.dumps(events))

    def test_rejects_unbounded_or_incomplete_messages(self):
        for raw in (b"x" * 8193, b"{}", b"[]\n", b"\xff\n"):
            with self.assertRaises(bridge.UserError):
                bridge.read_message(io.BytesIO(raw))
