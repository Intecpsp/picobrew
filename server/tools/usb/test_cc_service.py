"""CH340 service exercises ROM handshakes, per-unit enrollment and stock return without appliances."""
import contextlib
import errno
import hashlib
import io
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "firmware/pico-s-pro/modified"
sys.path.insert(0, str(COMPONENT/"tools"))
sys.path.insert(0, str(COMPONENT/"tests"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cc_service as service
from cc3200tool import cc
from test_pico_flash_install import FakeInstallUSB, fixture_images, PassthroughReceive


class ServiceTests(unittest.TestCase):
    def test_com_selection_is_revalidated_and_never_requires_winusb(self):
        from serial.tools import list_ports
        def port(path, uid):
            return SimpleNamespace(device=path, vid=0x1a86, pid=0x7523, serial_number=uid, location="HUB_1")
        with mock.patch.object(service.sys, "platform", "win32"), \
             mock.patch.object(list_ports, "comports", return_value=[port("COM12","a"),port("COM13","b")]) as ports, \
             mock.patch.object(service, "_usb_devices", side_effect=AssertionError):
            found=service.devices()
            self.assertEqual(len(found),2)
            self.assertNotEqual(found[0]["port"], found[1]["port"])
            adapter=service._transport(found[0]["port"])
            self.assertEqual(adapter.port_name,"COM12")
            self.assertTrue(adapter.exclusive)
            self.assertEqual(adapter.baud, 921600)
            ports.return_value=[port("COM12","different")]
            with self.assertRaisesRegex(service.CCError,"disconnected"):
                service._transport(found[0]["port"])

    def test_two_pass_install_corrupt_source_rejection_and_verified_restore(self):
        i=service.install
        original=fixture_images()
        for name in (i.APP_NAME,i.RECOVERY_NAME):
            original[name]=(COMPONENT.parent/"oem/device"/Path(name).name).read_bytes()
        original["sys/uid.txt"]=b"ab"*16
        devices=[]
        def open_fake(_):
            f = FakeInstallUSB(devices[-1].files if devices else original)
            f.writable_names=(*f.writable_names,i.RECOVERY_NAME)
            devices.append(f)
            return f
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.object(service,"_transport",side_effect=open_fake), \
             mock.patch.object(service.backup,"QueuedUSBReceive",PassthroughReceive), \
             mock.patch.dict(i.preflight.EXPECTED_FILES,{i.LOADER_NAME:i._image_metadata(original[i.LOADER_NAME])}), \
             mock.patch.object(cc.time,"sleep"), mock.patch.object(cc.log,"disabled",True), \
             contextlib.redirect_stdout(io.StringIO()):
            data=Path(tmp)
            events=[]
            jobkey=data/"device-key"
            service.run("install","pico-s","test","",data,events.append,events.append,job_key_path=jobkey)
            last=devices[-1].files
            self.assertEqual(last[i.RECOVERY_NAME],i.RECOVERY_FILE.read_bytes())
            self.assertEqual(last[i.APP_NAME],(i.RECOVERY_FILE.parent/"pico-s/pico-s-standalone.bin").read_bytes())
            key=last[i.KEY_NAME]
            self.assertEqual(service.private.read_regular(jobkey,len(key),private=True),key)
            self.assertNotIn(key.strip().decode(),str(events))
            self.assertEqual(service.private.read_regular(data/"device-uid",33,private=True),b"ab"*16+b"\n")
            self.assertEqual(sum("RESET button now" in s for s in events),4)
            uid=hashlib.sha256(original["sys/uid.txt"]).hexdigest()
            source,=(data/"stock-firmware/pico-s"/uid).glob("*.bin")
            service.private.assert_private(source)
            before_wrong=len(devices)
            with self.assertRaisesRegex(service.CCError,"different identity"):
                service.run("restore","pico-s","test","",data,events.append,events.append,
                            expected_uid="cd"*16)
            self.assertTrue(all(not d.mutations for d in devices[before_wrong:]))
            service.run("restore","pico-s","test","",data,events.append,events.append,
                        expected_uid="ab"*16)
            for name in (i.APP_NAME,i.RECOVERY_NAME):
                self.assertEqual(devices[-1].files[name],original[name])
            data_before=source.read_bytes()
            with open(source,"r+b") as f:
                f.seek(600); f.write(b"bad")
            start=len(devices)
            with self.assertRaises((service.CCError,i.InstallError)):
                service.run("restore","pico-s","test","",data,events.append,events.append)
            self.assertTrue(all(not d.mutations for d in devices[start:]))
            self.assertNotEqual(source.read_bytes(),data_before)

    @unittest.skipUnless(os.name=="posix","PTY requires a POSIX host")
    def test_system_serial_driver_reads_fragmented_rom_version(self):
        import pty
        master,slave=pty.openpty()
        try:
            # Darwin's pseudo terminal lacks the IOSSIOSPEED arbitrary-baud
            # ioctl. Exercise framing there at a POSIX standard baud.
            baud = 115200 if sys.platform == "darwin" else 921600
            with service.probe.SerialTransport(os.ttyname(slave),baud,exclusive=True) as raw, contextlib.ExitStack() as stack:
                # Linux user-mode emulation may expose a PTY but omit its BREAK
                # ioctl. Emulate only that electrical signal on this test PTY.
                try:
                    raw.break_condition=True
                    raw.break_condition=False
                except OSError as exc:
                    if exc.errno!=errno.ENOTTY:
                        raise
                    stack.enter_context(mock.patch.object(raw.serial, "_update_break_state"))
                    raw.break_condition=False
                release=threading.Event()
                errors=[]
                def peer():
                    try:
                        assert release.wait(2)
                        os.write(master,b"\0\xcc")
                        assert os.read(master,4)==service.probe.GET_VERSION
                        payload=bytearray(28); payload[:4]=bytes([0,4,1,2]); payload[16]=0x10
                        os.write(master,b"\0\xcc\0\x1e")
                        os.write(master,bytes([sum(payload)&255])+payload)
                        assert os.read(master,2)==b"\0\xcc"
                    except BaseException as exc:
                        errors.append(exc)
                task=threading.Thread(target=peer,daemon=True);task.start()
                def message(line):
                    if line.startswith("BREAK asserted."): release.set()
                version=service.probe.probe(raw,2,report=message)
                task.join(2)
                self.assertFalse(task.is_alive() or errors)
                self.assertEqual(version[:4],bytes([0,4,1,2]))
                with service.backup.receive_channel(raw) as receiver:
                    self.assertIs(raw,receiver)
        finally:
            os.close(master);os.close(slave)


if __name__=="__main__":
    unittest.main()
