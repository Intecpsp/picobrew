"""Native port checks used by the disposable server process smoke."""
import ctypes
import socket
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_server_start as smoke


class PortCheckTests(unittest.TestCase):
    def test_windows_table_layout_port_order_and_resize_handshake(self):
        self.assertEqual(ctypes.sizeof(smoke.WindowsTCP4Row), 24)
        self.assertEqual(ctypes.sizeof(smoke.WindowsTCP6Row), 56)
        ipv4 = smoke.WindowsTCP4Row(state=2, local_port=socket.htons(8080))
        ipv6 = smoke.WindowsTCP6Row(state=2, local_port=socket.htons(9090))
        tables = {socket.AF_INET: bytes(ipv4), socket.AF_INET6: bytes(ipv6)}
        calls = []

        def query(buffer, size_pointer, ordered, family, table_class, reserved):
            self.assertFalse(ordered)
            self.assertEqual((table_class, reserved), (3, 0))
            calls.append(family)
            data = bytes(ctypes.c_uint32(1)) + tables[family]
            size = size_pointer._obj
            if buffer is None or size.value < len(data):
                size.value = len(data)
                return 122
            ctypes.memmove(buffer, data, len(data))
            size.value = len(data)
            return 0

        with patch.object(ctypes, 'WinDLL', return_value=SimpleNamespace(GetExtendedTcpTable=query),
                          create=True):
            self.assertEqual(smoke.windows_listener_ports(), {8080, 9090})
        self.assertEqual(calls, [socket.AF_INET, socket.AF_INET, socket.AF_INET6, socket.AF_INET6])
        self.assertEqual(query.restype, ctypes.c_uint32)
        self.assertEqual(len(query.argtypes), 6)

    def test_windows_table_retries_are_bounded(self):
        calls = []

        def query(buffer, size_pointer, *arguments):
            calls.append(buffer)
            size_pointer._obj.value += 24
            return 122

        with patch.object(ctypes, 'WinDLL', return_value=SimpleNamespace(GetExtendedTcpTable=query),
                          create=True):
            with self.assertRaisesRegex(RuntimeError, 'kept changing'):
                smoke.windows_listener_ports()
        self.assertEqual(len(calls), 4)

    def test_preflight_rejects_an_existing_listener_without_connecting(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            port = listener.getsockname()[1]
            with self.assertRaisesRegex(RuntimeError, 'already in use'):
                smoke.free_port(port)
            listener.settimeout(.05)
            with self.assertRaises(TimeoutError):
                listener.accept()

    def test_native_listener_is_detected_until_its_socket_closes(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            port = listener.getsockname()[1]
            self.assertFalse(smoke.listener_closed(port))
        self.assertTrue(smoke.listener_closed(port))
        smoke.free_port(port)

    def test_time_wait_is_not_an_orphan_listener(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            if not smoke.WINDOWS:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            port = listener.getsockname()[1]
            with socket.create_connection(('127.0.0.1', port), timeout=2) as client:
                peer, _ = listener.accept()
                peer.close()  # Server initiates the close, entering TIME_WAIT.
                self.assertEqual(client.recv(1), b'')
        self.assertTrue(smoke.listener_closed(port))
        smoke.free_port(port)

    @unittest.skipUnless(smoke.WINDOWS and socket.has_ipv6, 'Native Windows IPv6 listener table')
    def test_windows_checks_ipv6_and_wildcard_listeners_too(self):
        with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as listener:
            listener.bind(('::', 0))
            listener.listen()
            port = listener.getsockname()[1]
            self.assertFalse(smoke.listener_closed(port))
            with self.assertRaisesRegex(RuntimeError, 'already in use'):
                smoke.free_port(port)
        self.assertTrue(smoke.listener_closed(port))

    def test_failed_or_timed_out_inspection_does_not_prove_shutdown(self):
        with patch.object(smoke, 'WINDOWS', False), \
                patch.object(socket, 'create_connection', side_effect=TimeoutError('probe timeout')):
            with self.assertRaises(TimeoutError):
                smoke.listener_closed()
        with patch.object(smoke, 'WINDOWS', True), \
                patch.object(smoke, 'windows_listener_ports', side_effect=OSError('native inspection failed')):
            with self.assertRaises(OSError):
                smoke.listener_closed()


if __name__ == '__main__':
    unittest.main(verbosity=2)
