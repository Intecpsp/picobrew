"""Compare compiled MSP reconstruction with captured original Xtensa executions.

The fixtures come from EmulateMsp.java, not a second implementation of the C
algorithms. The normal test run needs only Python and a native C compiler;
regenerating original executions uses the separate pinned Ghidra runner.
"""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest

import run_msp_emulation as evidence

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/msp-original.json"
PROVENANCE = ROOT / "analysis/msp-protocol-validation.json"
U8 = C.c_uint8
U32 = C.c_uint32
P8 = C.POINTER(U8)


class State(C.Structure):
    _fields_ = [("id", U32), ("use_flags", U8), ("pumps", U8), ("fan", U8),
                ("valve_position", U8), ("status_byte_2", U8), ("status_byte_3", U8),
                ("errors", U32), ("moving_valve", U8), ("version_major", U8),
                ("version_minor", U8), ("last_flags", U8)]

    @classmethod
    def from_original(cls, data):
        raw = bytes.fromhex(data) if isinstance(data, str) else bytes(data)
        return cls(struct.unpack_from("<I", raw, 4)[0], *raw[8:14],
                   struct.unpack_from("<I", raw, 16)[0], *raw[20:24])

    def to_original(self, before):
        raw = bytearray.fromhex(before) if isinstance(before, str) else bytearray(before)
        struct.pack_into("<I", raw, 4, self.id)
        raw[8:14] = bytes((self.use_flags, self.pumps, self.fan, self.valve_position,
                          self.status_byte_2, self.status_byte_3))
        struct.pack_into("<I", raw, 16, self.errors)
        raw[20:24] = bytes((self.moving_valve, self.version_major, self.version_minor, self.last_flags))
        return raw.hex()


TX = C.CFUNCTYPE(None, C.c_void_p, U8, P8, C.c_size_t)
RX = C.CFUNCTYPE(None, C.c_void_p, U8, P8, C.c_size_t)
DELAY = C.CFUNCTYPE(None, C.c_void_p, U32)


class Transport(C.Structure):
    _fields_ = [("context", C.c_void_p), ("transmit", TX), ("receive", RX), ("delay_ms", DELAY)]


class Client(C.Structure):
    _fields_ = [("state", C.POINTER(State)), ("transport", Transport), ("address", U8),
                ("response", U8 * 9), ("next_poll", U32), ("poll_interval", U32),
                ("transition_delay", U32), ("response_delay", U32), ("last_bsl_response", U8)]


class Segment(C.Structure):
    _fields_ = [("address", U32), ("size", U32), ("bytes", P8)]


class Image(C.Structure):
    _fields_ = [("segment_count", U32), ("segments", C.POINTER(Segment))]


def array(data):
    return (U8 * max(1, len(data)))(*data)


class Harness:
    def __init__(self, library, state=None, responses=()):
        self.events = []
        self.errors = []
        self.responses = list(responses)
        self.state = State(id=2) if state is None else state

        def transmit(context, address, data, count):
            self.events.append(["tx", address, C.string_at(data, count).hex()])

        def receive(context, address, data, count):
            self.events.append(["rx", address, count])
            if not self.responses:
                self.errors.append("C requested an unprovided response")
                payload = bytes(count)
            else:
                payload = self.responses.pop(0)
                if len(payload) != count:
                    self.errors.append(f"C requested {count} bytes for fixture of {len(payload)}")
                    payload = (payload + bytes(count))[:count]
            C.memmove(data, payload, count)

        def delay(context, milliseconds):
            self.events.append(["delay", milliseconds])

        self.transmit = TX(transmit)
        self.receive = RX(receive)
        self.delay = DELAY(delay)
        self.transport = Transport(None, self.transmit, self.receive, self.delay)
        self.client = Client()
        result = library.z_msp_init(C.byref(self.client), C.byref(self.state), self.transport,
                                   0x48, 41, 17, 23)
        if result != 0:
            raise AssertionError("Fixture client did not initialize")


class MspTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixtures = json.loads(FIXTURES.read_text())
        cls.temporary = tempfile.TemporaryDirectory(prefix="z-msp-native-")
        target = Path(cls.temporary.name) / "msp.so"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-O2", "-fPIC", "-shared", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(ROOT / "include"), str(ROOT / "src/msp_protocol.c"), "-o", str(target)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        cls.lib = C.CDLL(str(target))

        def declare(name, args, result=C.c_int):
            function = getattr(cls.lib, name)
            function.argtypes = args
            function.restype = result

        ps, pc, pi = C.POINTER(State), C.POINTER(Client), C.POINTER(Image)
        declare("z_msp_crc_byte", [C.c_uint16, U8], C.c_uint16)
        declare("z_msp_crc", [P8, C.c_size_t], C.c_uint16)
        declare("z_msp_image_crc", [pi], C.c_uint16)
        declare("z_msp_apply_response", [ps, P8], None)
        declare("z_msp_command_allowed", [ps, U8])
        declare("z_msp_is_boot_response", [P8], C.c_bool)
        declare("z_msp_is_expected_version", [P8], C.c_bool)
        declare("z_msp_app_frame", [P8, C.c_size_t, U8, U32, C.c_uint], C.c_size_t)
        declare("z_msp_bsl_simple_frame", [P8, C.c_size_t, U8], C.c_size_t)
        declare("z_msp_bsl_program_frame", [P8, C.c_size_t, U32, P8, C.c_size_t], C.c_size_t)
        declare("z_msp_bsl_erase_frame", [P8, C.c_size_t, U32], C.c_size_t)
        declare("z_msp_init", [pc, ps, Transport, U8, U32, U32, U32])
        declare("z_msp_send", [pc, U8, U32, C.c_uint])
        declare("z_msp_poll", [pc, U32])
        declare("z_msp_move_valve", [pc, U8])
        declare("z_msp_step_valve", [pc, C.c_bool])
        declare("z_msp_bsl_command", [pc, U8])
        declare("z_msp_program_segment", [pc, U32, P8, U32])
        declare("z_msp_erase_app", [pc])
        for function in ("z_msp_switch_to_boot", "z_msp_start_app", "z_msp_get_version"):
            declare(function, [pc])
        declare("z_msp_update", [pc, pi])
        declare("z_msp_setup", [pc, pi])

        raw = evidence.INPUT.read_bytes()
        parsed = evidence.images.parse_image(raw)
        cls.segments = [{"address": s["load_address"], "offset": s["file_offset"], "size": s["size_bytes"]}
                        for s in parsed["segments"]]
        cls.raw = raw
        cls.images = {}
        cls.image_resources = []
        for descriptor, unused_crc in cls.fixtures["image_crc"]:
            count, pointer = struct.unpack("<II", evidence.read_mapped(raw, cls.segments, descriptor, 8))
            records = (Segment * count)()
            for i in range(count):
                address, size, payload = struct.unpack("<III", evidence.read_mapped(raw, cls.segments, pointer + 12 * i, 12))
                content = array(evidence.read_mapped(raw, cls.segments, payload, size))
                records[i] = Segment(address, size, content)
                cls.image_resources.append(content)
            cls.images[descriptor] = Image(count, records)
            cls.image_resources.append(records)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def assert_events(self, harness, expected):
        self.assertEqual(harness.errors, [])
        self.assertEqual(harness.events, expected)

    def test_fixture_provenance_pins_original_bytes_and_generator(self):
        self.assertEqual(self.fixtures["mode"], "full")
        self.assertFalse(self.fixtures["full_cpu_emulation"])
        self.assertFalse(self.fixtures["hardware_tested"])
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), self.fixtures["input_sha256"])
        provenance = json.loads(PROVENANCE.read_text())
        self.assertEqual(provenance["fixture_sha256"], hashlib.sha256(FIXTURES.read_bytes()).hexdigest())
        for relative, expected in provenance["generator_files"].items():
            self.assertEqual(hashlib.sha256((ROOT / relative).read_bytes()).hexdigest(), expected, relative)
        for function in self.fixtures["original_functions"]:
            for span in function["ranges"]:
                actual = evidence.read_mapped(self.raw, self.segments, span["address"], span["size"])
                self.assertEqual(hashlib.sha256(actual).hexdigest(), span["sha256"], hex(function["entry"]))
        self.assertEqual(len(self.fixtures["original_functions"]), 24)

    def test_crc_matches_original_loop_for_every_byte_and_eight_seeds(self):
        self.assertEqual(len(self.fixtures["crc_bytes"]), 2048)
        for seed, byte, expected in self.fixtures["crc_bytes"]:
            self.assertEqual(self.lib.z_msp_crc_byte(seed, byte), expected, (seed, byte))
        check = b"123456789"
        self.assertEqual(self.lib.z_msp_crc(array(check), len(check)), 0x29b1)

    def test_embedded_msp_images_match_original_address_range_crc(self):
        for descriptor, expected in self.fixtures["image_crc"]:
            self.assertEqual(self.lib.z_msp_image_crc(C.byref(self.images[descriptor])), expected)
        self.assertEqual(dict(self.fixtures["image_crc"])[0x3f425ddc], 0x5fdb)
        self.assertEqual(dict(self.fixtures["image_crc"])[0x3f425578], 0xc60a)

    def test_application_guard_matches_original_assertions(self):
        codes = {0: 0, 0x70: -2, 0x75: -3}
        for state_id, moving, command, assertion in self.fixtures["guards"]:
            state = State(id=state_id, moving_valve=moving)
            self.assertEqual(self.lib.z_msp_command_allowed(C.byref(state), command), codes[assertion],
                             (state_id, moving, command))

    def test_response_decoding_matches_original_memory_writes(self):
        self.assertEqual(len(self.fixtures["responses"]), 512)
        for response, before, after, events in self.fixtures["responses"]:
            state = State.from_original(before)
            self.lib.z_msp_apply_response(C.byref(state), array(bytes.fromhex(response)))
            self.assertEqual(state.to_original(before), after, response)
            self.assertEqual(events, [["rx", 72, 9]])

    def test_all_application_commands_match_original_bytes_and_io_order(self):
        self.assertEqual(len(self.fixtures["app_packets"]), 768)
        for command, width, value, events in self.fixtures["app_packets"]:
            harness = Harness(self.lib, responses=[bytes(9)])
            self.assertEqual(self.lib.z_msp_send(C.byref(harness.client), command, value, width), 0)
            self.assert_events(harness, events)
            out = (U8 * 9)()
            count = self.lib.z_msp_app_frame(out, len(out), command, value, width)
            self.assertEqual(bytes(out[:count]).hex(), events[0][2])

    def test_bootloader_simple_commands_and_start_without_ack(self):
        for command, reply, expected, events in self.fixtures["bsl_simple"]:
            responses = [] if command == 0x1c else [bytes([reply])]
            harness = Harness(self.lib, state=State(id=1), responses=responses)
            self.assertEqual(self.lib.z_msp_bsl_command(C.byref(harness.client), command), expected)
            self.assert_events(harness, events)

    def test_program_chunks_address_truncation_and_error_stop_match_original(self):
        for address, content, failed_chunk, expected, events in self.fixtures["program_segments"]:
            payload = bytes.fromhex(content)
            replies = [bytes([5 if i == failed_chunk else 0]) for i in range(1, (len(payload) + 15) // 16 + 1)]
            harness = Harness(self.lib, state=State(id=1), responses=replies)
            result = self.lib.z_msp_program_segment(C.byref(harness.client), address, array(payload), len(payload))
            self.assertEqual(result, expected, (address, len(payload), failed_chunk))
            self.assert_events(harness, events)

    def test_erase_range_and_failure_stop_match_original(self):
        for failed_segment, assertion, events in self.fixtures["erase"]:
            replies = [bytes([5 if i == failed_segment else 0]) for i in range(1, 31)]
            harness = Harness(self.lib, state=State(id=1), responses=replies)
            self.assertEqual(self.lib.z_msp_erase_app(C.byref(harness.client)), -9 if assertion == 255 else 0)
            self.assert_events(harness, events)

    def test_valve_range_and_busy_guard_and_post_response_moving_flag(self):
        codes = {0: 0, 0x340: -4, 0x70: -2, 0x75: -3}
        for position, before, assertion, after, events in self.fixtures["valve"]:
            harness = Harness(self.lib, state=State.from_original(before), responses=[bytes(9)])
            self.assertEqual(self.lib.z_msp_move_valve(C.byref(harness.client), position), codes[assertion])
            self.assertEqual(harness.state.to_original(before), after)
            self.assert_events(harness, events)

    def test_poll_strict_deadline_and_uint32_wrap_match_original(self):
        for state_id, deadline, now, expected_deadline, events in self.fixtures["poll"]:
            harness = Harness(self.lib, state=State(id=state_id, moving_valve=1), responses=[bytes(9)])
            harness.client.next_poll = deadline
            self.assertEqual(self.lib.z_msp_poll(C.byref(harness.client), now), 0)
            self.assertEqual(harness.client.next_poll, expected_deadline)
            self.assert_events(harness, events)

    def test_setup_and_update_sequences_match_original_including_failures(self):
        self.assertEqual(len(self.fixtures["setup"]), 11)
        outcomes = {0: -8, 1: -8, 2: 0, 3: -5, 4: 0, 5: 0, 6: -7, 7: 0, 8: 0, 9: -9, 10: -7}
        original_assertions = {6: 0x2f0, 9: 0xff, 10: 0x2f1}
        for scenario, assertion, state_after, events, received in self.fixtures["setup"]:
            with self.subTest(scenario=scenario):
                harness = Harness(self.lib, state=State(), responses=[bytes.fromhex(data) for data in received])
                result = self.lib.z_msp_setup(C.byref(harness.client), C.byref(self.images[0x3f425ddc]))
                self.assertEqual(result, outcomes[scenario])
                self.assertEqual(assertion, original_assertions.get(scenario, 0))
                self.assertEqual(harness.state.to_original(bytes(24)), state_after)
                self.assert_events(harness, events)

    def test_portable_frame_interface_rejects_undersized_buffers_without_writes(self):
        # These are new portable API boundaries, not claimed OEM assertion cases.
        out = (U8 * 24)(*([0xa5] * 24))
        for width, size in ((0, 4), (1, 5), (4, 8), (2, 24), (3, 24)):
            self.assertEqual(self.lib.z_msp_app_frame(out, size, 0xfd, 0, width), 0)
            self.assertEqual(bytes(out), b"\xa5" * 24)
        for count, size in ((0, 24), (17, 24), (16, 23)):
            self.assertEqual(self.lib.z_msp_bsl_program_frame(out, size, 0xc002, array(bytes(17)), count), 0)
            self.assertEqual(bytes(out), b"\xa5" * 24)


if __name__ == "__main__":
    unittest.main()
