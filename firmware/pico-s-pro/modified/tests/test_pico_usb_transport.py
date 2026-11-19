#!/usr/bin/env python3
"""USB ownership and native ABI checks; never enumerate/open a physical device.

The native test loads the packaged library and allocates unsubmitted transfers.
All adapter enumeration, Windows SetupAPI calls, claims and I/O are mocked.
"""
import ctypes
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import libusb_package
import usb.backend.libusb1 as abi
import usb.core
import usb.util

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

import pico_uart_probe as probe
from pico_flash_backup_v2 import _NativeUSBReceiveDriver
from pico_fake_async_usb import FakeBackend, Function


class FakeSetupAPI:
    """Documented SetupAPI buffers, with synthetic present-device properties."""

    def __init__(self, devices, enumerate_error=None):
        self.devices = devices
        self.error = 0
        self.enumerate_error = enumerate_error
        self.destroyed = 0
        self.SetupDiGetClassDevsW = Function(self.open)
        self.SetupDiEnumDeviceInfo = Function(self.enumerate)
        self.SetupDiGetDeviceRegistryPropertyW = Function(self.property)
        self.SetupDiDestroyDeviceInfoList = Function(self.close)

    def open(self, guid, enumerator, parent, flags):
        assert guid is None and enumerator == "USB" and parent is None
        assert flags == 6
        return 0x1234

    def info(self, pointer):
        pointer_type = self.SetupDiEnumDeviceInfo.argtypes[2]
        return ctypes.cast(pointer, pointer_type).contents

    def enumerate(self, handle, index, pointer):
        assert handle == 0x1234
        if self.enumerate_error or index >= len(self.devices):
            self.error = self.enumerate_error or 259
            return 0
        info = self.info(pointer)
        assert info.cbSize == (32 if ctypes.sizeof(ctypes.c_void_p) == 8 else 28)
        info.DevInst = index + 1
        return 1

    def property(self, handle, pointer, property_id, kind, buffer, capacity, required):
        assert handle == 0x1234
        hardware_ids, service = self.devices[self.info(pointer).DevInst - 1]
        value = hardware_ids if property_id == 1 else service
        if value is None:
            self.error = 13
            return 0
        text = ("\0".join(value) + "\0\0") if property_id == 1 else value + "\0"
        data = text.encode("utf-16-le")
        ctypes.cast(required, ctypes.POINTER(ctypes.c_uint32)).contents.value = len(data)
        ctypes.cast(kind, ctypes.POINTER(ctypes.c_uint32)).contents.value = 7 if property_id == 1 else 1
        if not capacity:
            self.error = 122
            return 0
        assert capacity >= len(data)
        ctypes.memmove(buffer, data, len(data))
        return 1

    def close(self, handle):
        assert handle == 0x1234
        self.destroyed += 1
        return 1


class WindowsDriverTests(unittest.TestCase):
    HARDWARE_IDS = ["USB\\VID_1A86&PID_7523&REV_0264", "USB\\VID_1A86&PID_7523"]

    def service(self, api):
        with patch.object(ctypes, "WinDLL", return_value=api, create=True), \
                patch.object(ctypes, "get_last_error", side_effect=lambda: api.error, create=True):
            return probe._windows_driver_service(0x1A86, 0x7523)

    def test_present_winusb_driver_is_required_and_registry_handle_is_closed(self):
        api = FakeSetupAPI([(["USB\\VID_1234&PID_5678"], "usbser"),
                            (self.HARDWARE_IDS, "WinUSB")])
        self.assertEqual(self.service(api), "WinUSB")
        self.assertEqual(api.destroyed, 1)
        self.assertIs(api.SetupDiGetClassDevsW.restype, ctypes.c_void_p)

    def test_com_shared_libusb_and_missing_bindings_are_rejected(self):
        for service in ("CH341SER_A64", "usbser", "libusbK", "libusb0", None):
            with self.subTest(service=service):
                api = FakeSetupAPI([(self.HARDWARE_IDS, service)])
                with self.assertRaisesRegex(probe.CH340USBError, "requires the WinUSB driver"):
                    self.service(api)
                self.assertEqual(api.destroyed, 1)

    def test_missing_multiple_and_near_match_adapters_are_rejected(self):
        for devices in ([], [(self.HARDWARE_IDS, "WinUSB")] * 2,
                        [(["USB\\VID_1A86&PID_75230"], "WinUSB")]):
            with self.subTest(devices=devices):
                api = FakeSetupAPI(devices)
                with self.assertRaisesRegex(probe.CH340USBError, "leave only the intended"):
                    self.service(api)
                self.assertEqual(api.destroyed, 1)

    def test_registry_inspection_failure_is_closed_and_not_assumed_safe(self):
        api = FakeSetupAPI([], enumerate_error=5)
        with self.assertRaisesRegex(probe.CH340USBError, "error 5"):
            self.service(api)
        self.assertEqual(api.destroyed, 1)


class DescriptorList(list):
    def __init__(self, entries, **fields):
        super().__init__(entries)
        self.__dict__.update(fields)


class TransportOwnershipTests(unittest.TestCase):
    def setUp(self):
        interface = DescriptorList([
            SimpleNamespace(bmAttributes=2, bEndpointAddress=0x82, wMaxPacketSize=32),
            SimpleNamespace(bmAttributes=2, bEndpointAddress=0x02, wMaxPacketSize=32),
        ], bInterfaceClass=0xFF, bAlternateSetting=0, bInterfaceNumber=0)
        self.device = SimpleNamespace(
            bus=1, address=2,
            get_active_configuration=Mock(return_value=DescriptorList([interface], bConfigurationValue=1)),
            is_kernel_driver_active=Mock(side_effect=AssertionError("Windows must not query kernel ownership")),
            detach_kernel_driver=Mock(), attach_kernel_driver=Mock(),
        )
        self.backend = SimpleNamespace(lib=None)
        self.find = self.enter_patch(patch.object(usb.core, "find", return_value=[self.device]))
        self.enter_patch(patch.object(libusb_package, "get_libusb1_backend", return_value=self.backend))
        self.enter_patch(patch.object(abi, "get_backend", side_effect=AssertionError("Unexpected native backend")))
        self.enter_patch(patch.object(probe.CH340USBTransport, "_check_libusb_version"))
        self.service = self.enter_patch(patch.object(probe, "_windows_driver_service", return_value="WinUSB"))
        self.claim = self.enter_patch(patch.object(usb.util, "claim_interface"))
        self.release = self.enter_patch(patch.object(usb.util, "release_interface"))
        self.dispose = self.enter_patch(patch.object(usb.util, "dispose_resources"))
        self.configure = self.enter_patch(patch.object(probe.CH340USBTransport, "_configure"))
        self.platform = self.enter_patch(patch.object(probe, "sys", SimpleNamespace(platform="win32")))

    def enter_patch(self, patcher):
        result = patcher.start()
        self.addCleanup(patcher.stop)
        return result

    def test_windows_claims_before_uart_configuration_and_never_changes_driver(self):
        def check_claimed():
            self.claim.assert_called_once_with(self.device, 0)
        self.configure.side_effect = check_claimed
        with probe.CH340USBTransport() as raw:
            self.assertTrue(raw._claimed)
            self.assertEqual(raw.details["windows_driver_service"], "WinUSB")
            self.assertIsNone(raw.details["kernel_driver_was_active"])
        self.device.is_kernel_driver_active.assert_not_called()
        self.device.detach_kernel_driver.assert_not_called()
        self.device.attach_kernel_driver.assert_not_called()
        self.release.assert_called_once_with(self.device, 0)
        self.dispose.assert_called_once_with(self.device)

    def test_windows_wrong_driver_stops_before_open_or_uart_configuration(self):
        self.service.side_effect = probe.CH340USBError("requires the WinUSB driver")
        with self.assertRaisesRegex(probe.CH340USBError, "WinUSB"):
            with probe.CH340USBTransport():
                self.fail("Driver prerequisite must stop the transport")
        self.find.assert_not_called()
        self.device.get_active_configuration.assert_not_called()
        self.claim.assert_not_called()
        self.configure.assert_not_called()

    def test_busy_winusb_handle_fails_before_uart_and_releases_resources(self):
        self.device.get_active_configuration.side_effect = usb.core.USBError("busy")
        with self.assertRaisesRegex(probe.CH340USBError, "Close other programs"):
            with probe.CH340USBTransport():
                self.fail("Busy device must not open")
        self.claim.assert_not_called()
        self.configure.assert_not_called()
        self.device.attach_kernel_driver.assert_not_called()
        self.dispose.assert_called_once_with(self.device)

    def test_failed_claim_does_not_configure_or_release_an_unowned_interface(self):
        self.claim.side_effect = usb.core.USBError("busy")
        with self.assertRaisesRegex(probe.CH340USBError, "exclusive access"):
            with probe.CH340USBTransport():
                self.fail("Claim must succeed before UART commands")
        self.configure.assert_not_called()
        self.release.assert_not_called()
        self.device.attach_kernel_driver.assert_not_called()
        self.dispose.assert_called_once_with(self.device)

    def test_failed_uart_configuration_releases_windows_claim_without_driver_changes(self):
        self.configure.side_effect = probe.CH340USBError("configuration failed")
        with self.assertRaisesRegex(probe.CH340USBError, "configuration failed"):
            with probe.CH340USBTransport():
                self.fail("Configuration should fail")
        self.release.assert_called_once_with(self.device, 0)
        self.device.attach_kernel_driver.assert_not_called()
        self.dispose.assert_called_once_with(self.device)

    def test_unix_only_reattaches_a_driver_that_this_session_detached(self):
        self.platform.platform = "linux"
        self.device.is_kernel_driver_active.side_effect = None
        self.device.is_kernel_driver_active.return_value = True
        with probe.CH340USBTransport():
            self.device.detach_kernel_driver.assert_called_once_with(0)
        self.device.attach_kernel_driver.assert_called_once_with(0)
        self.service.assert_not_called()

    def test_unix_restores_driver_if_claim_fails_after_detaching(self):
        self.platform.platform = "linux"
        self.device.is_kernel_driver_active.side_effect = None
        self.device.is_kernel_driver_active.return_value = True
        self.claim.side_effect = usb.core.USBError("busy")
        with self.assertRaisesRegex(probe.CH340USBError, "exclusive access"):
            with probe.CH340USBTransport():
                self.fail("Claim should fail")
        self.device.attach_kernel_driver.assert_called_once_with(0)
        self.configure.assert_not_called()
        self.release.assert_not_called()

    def test_unix_free_interface_is_not_detached_or_reattached(self):
        self.platform.platform = "linux"
        self.device.is_kernel_driver_active.side_effect = None
        self.device.is_kernel_driver_active.return_value = False
        with probe.CH340USBTransport():
            pass
        self.device.detach_kernel_driver.assert_not_called()
        self.device.attach_kernel_driver.assert_not_called()

    def test_unix_unknown_driver_ownership_fails_closed(self):
        self.platform.platform = "linux"
        self.device.is_kernel_driver_active.side_effect = NotImplementedError
        with self.assertRaisesRegex(probe.CH340USBError, "refusing to assume"):
            with probe.CH340USBTransport():
                self.fail("Unknown ownership must stop the transport")
        self.claim.assert_not_called()
        self.configure.assert_not_called()


class NativeLibraryTests(unittest.TestCase):
    def test_native_version_allocation_and_callback_use_host_abi_without_usb_access(self):
        # Loading a DLL and allocating an unsubmitted transfer do not initialize
        # libusb, enumerate USB devices, open a handle, or send any USB request.
        path = libusb_package.get_library_path()
        self.assertIsNotNone(path, "Install the pinned libusb-package dependency")
        loader = ctypes.WinDLL if sys.platform == "win32" else ctypes.CDLL
        library = loader(str(path))
        backend = FakeBackend(library)
        backend.ctx = None
        raw = SimpleNamespace(_backend=backend, _in_ep=0x82,
                              _device=SimpleNamespace(_ctx=SimpleNamespace(
                                  handle=SimpleNamespace(handle=None))))
        transport = probe.CH340USBTransport()
        transport._backend = backend
        transport._check_libusb_version()
        self.assertRegex(transport.details["libusb_version"], r"^1\.0\.\d+$")
        driver = _NativeUSBReceiveDriver(raw)
        completions = []
        driver.set_callback(lambda *args, **kwargs: completions.append((args, kwargs)))
        native_type = ctypes.WINFUNCTYPE if sys.platform == "win32" else ctypes.CFUNCTYPE
        callback_type = native_type(None, ctypes.POINTER(abi._libusb_transfer))
        self.assertIsInstance(driver._callback_thunk, callback_type)
        self.assertEqual(ctypes.sizeof(driver._Timeval), ctypes.sizeof(ctypes.c_long) * 2)
        if sys.platform == "win32":
            self.assertEqual(ctypes.sizeof(driver._Timeval), 8)
            self.assertEqual(driver._callback_thunk._flags_ & ctypes._FUNCFLAG_CDECL, 0)
        slot = driver.allocate(288)
        try:
            payload = b"synthetic USB completion"
            ctypes.memmove(slot.pointer.contents.buffer, payload, len(payload))
            slot.pointer.contents.actual_length = len(payload)
            slot.pointer.contents.status = 0
            ctypes.cast(slot.pointer.contents.callback, callback_type)(slot.pointer)
            self.assertEqual(completions, [((slot.key, 0, payload), {})])
        finally:
            driver.free(slot)
        self.assertEqual(driver._slots, {})


if __name__ == "__main__":
    unittest.main(verbosity=2)
