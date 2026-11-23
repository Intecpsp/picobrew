import io
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import usb_bridge as bridge


class WorkerTests(unittest.TestCase):
    def test_list_hides_paths_and_serials_and_rejects_disconnected_target(self):
        serial = SimpleNamespace(vid=0x20a0,pid=0x421e,serial_number="private-serial",location="private-location",device="/dev/ttyUSB0")
        entries=bridge.serial_devices([serial])
        events=[]
        bridge.run({"action":"list"},None,events.append,lambda:entries)
        self.assertEqual(events[0]["type"],"devices")
        device=events[0]["devices"][0]
        self.assertEqual(device["models"],["zymatic"])
        self.assertEqual(set(device),{"label","port","models"})
        self.assertNotIn("private-",json.dumps(device))
        self.assertNotIn("/dev/",json.dumps(device))
        with self.assertRaisesRegex(bridge.UserError,"unavailable"):
            bridge.run({"action":"backup","model":"zymatic","port":device["port"]},None,events.append,lambda:{})

    def test_confirmations_cannot_be_replayed_and_keys_stay_private(self):
        import usb_avr
        import usb_stock
        entries={"serial-t":{"port":"serial-t","label":"Zymatic","models":["zymatic"],"path":"simulated"}}
        with tempfile.TemporaryDirectory() as tmp:
            request={"action":"install","data_dir":str(Path(tmp).resolve()),"job_id":"e"*32,"model":"zymatic","port":"serial-t"}
            events=[]
            with patch.object(usb_avr,"usb_identity",return_value=("simulated","one","location")),patch.object(usb_avr,"service") as service:
                with self.assertRaisesRegex(bridge.UserError,"canceled"):
                    bridge.run(request,io.BytesIO(b'{"confirm":"old"}\n'),events.append,lambda:entries)
                service.assert_not_called()
                events.clear()
                def complete(data, port, action, *, confirm,key_file,hub_host):
                    self.assertEqual(confirm("Type INSTALL ZYMATIC: "),"INSTALL ZYMATIC")
                    self.assertEqual(confirm("Type WRITE 1.2.0: "),"WRITE 1.2.0")
                    usb_stock.save_private(key_file,b'a'*64)
                    return 'b'*32
                service.side_effect=complete
                class Confirmations:
                    def readline(self, n):
                        return (json.dumps({"confirm":events[-1]["id"]})+"\n").encode()
                bridge.run(request,Confirmations(),events.append,lambda:entries)
                folder=Path(tmp)/"usb-jobs"/request["job_id"]
                self.assertEqual((folder/"device-key").read_bytes(),b'a'*64)
                self.assertEqual((folder/"device-uid").read_bytes(),b'b'*32)
                self.assertNotIn('a'*64,json.dumps(events))
                self.assertNotIn(str(Path(tmp)),json.dumps(events))
                self.assertEqual(len({e['id'] for e in events if e['type']=='confirm'}),3)

    def test_rejects_unbounded_or_incomplete_messages(self):
        for raw in (b"x"*8193, b"{}", b"[]\n", b"\xff\n"):
            with self.assertRaises(bridge.UserError):
                bridge.read_message(io.BytesIO(raw))
