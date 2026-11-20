#!/usr/bin/env python3
"""Compare recovered Core state, mailboxes, credentials and ordered service calls."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"
KINDS = ["transition", "wifi", "tick", "start", "mode"]
WORDS = ["state", "state_date", "service_mode", "rinse_pending", "offline", "session_type", "session_mode", "program_id",
         "runtime_start_pending", "persist_credentials", "ethernet_present", "download_requested", "session_stop_requested", "azure_enabled",
         "temperature_enabled", "service_button", "system_error", "msp_id", "msp_errors", "hex_date", "wort_date", "drain_date", "ambient_date",
         "sensor_error", "runtime_id", "encoder_date", "http_backend_state", "sync_done", "backend_error", "registered", "update_available",
         "update_byte_11b58", "update_byte_11b68", "update_client_state", "session_creation_error", "session_logger_state", "use_still_wifi",
         "config_ethernet", "config_azure", "config_still_mode", "config_ssid", "pending_ssid", "ethernet_address", "network_result",
         "now", "standby_delay", "boot_delay", "ethernet_timeout", "queue_mask", "wifi_type", "wifi_argument", "wifi_second_argument",
         "runtime_event", "runtime_previous", "runtime_current", "command_id", "button_id", "text_token", "still_event", "still_state",
         "target", "config_password", "pending_password", "assertion_line"]
FLAGS = ["-std=c++11", "-O2", "-fPIC", "-shared", "-fno-fast-math", "-ffp-contract=off"]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def case(name, kind="tick", **overrides):
    values = dict(state=12, state_date=100, session_type=6, session_mode=1, program_id=1,
                  msp_id=2, hex_date=100, wort_date=100, drain_date=100, ambient_date=100,
                  runtime_id=2, sync_done=1, registered=1, config_ssid=1, pending_ssid=1, ethernet_address=0x01020304,
                  network_result=1, now=2000, standby_delay=1800000, boot_delay=1100, ethernet_timeout=10000,
                  wifi_type=5, wifi_argument=2, wifi_second_argument=3, runtime_previous=3, runtime_current=2,
                  command_id=16, text_token=119, still_event=1, still_state=16, target=1, config_password=1, pending_password=1)
    values.update(overrides)
    return {"id": name, "kind": kind, "words": [values.get(key, 0) & 0xffffffff for key in WORDS]}


def input_cases():
    cases = []
    for previous in range(19):
        for next_state in range(19):
            cases.append(case(f"transition-{previous}-{next_state}", "transition", state=previous, target=next_state))
    for next_state in (1, 2, 3, 10, 13):
        for flags in range(8):
            cases.append(case(f"transition-flags-{next_state}-{flags}", "transition", state=0, target=next_state,
                              service_button=flags & 1, pending_ssid=(flags >> 1) & 1, sync_done=(flags >> 2) & 1,
                              session_mode=flags % 3, session_type=11 if flags & 1 else 6))
    for state in range(19):
        for event in range(8):
            cases.append(case(f"wifi-{state}-{event}", "wifi", state=state, wifi_type=event))
    for state in (2, 3, 5, 12):
        for flags in range(16):
            cases.append(case(f"wifi-flags-{state}-{flags}", "wifi", state=state, service_mode=flags & 1,
                              persist_credentials=(flags >> 1) & 1, use_still_wifi=(flags >> 2) & 1,
                              config_azure=1 if flags & 8 else 2, config_ethernet=1))
    for state in range(19):
        cases.append(case(f"tick-state-{state}", state=state))
    for state in (1, 12):
        for field, values in (("system_error", [0, 1]), ("msp_id", range(4)), ("msp_errors", [0, 1, 255]),
                              ("sensor_error", range(6)), ("hex_date", [0, 100]), ("wort_date", [0, 100]),
                              ("drain_date", [0, 100]), ("ambient_date", [0, 100]), ("service_mode", [0, 1]),
                              ("config_ssid", [0, 1]), ("config_ethernet", [0, 1]), ("rinse_pending", [0, 1])):
            for value in values:
                cases.append(case(f"state-field-{state}-{field}-{value}", state=state, **{field: value}))
    for state in (1, 12):
        for now in (0, 99, 100, 1199, 1200, 1201, 1800100, 1800101, 0xffffffff):
            cases.append(case(f"deadline-{state}-{now}", state=state, now=now, runtime_id=4, encoder_date=100))
    for state in (4, 7, 8, 9, 13, 15):
        for flags in range(32):
            cases.append(case(f"backend-flags-{state}-{flags}", state=state, backend_error=flags & 1,
                              sync_done=(flags >> 1) & 1, registered=(flags >> 2) & 1, update_available=(flags >> 3) & 1,
                              http_backend_state=(flags >> 4) & 1, session_creation_error=flags & 1,
                              session_logger_state=flags % 3, session_mode=flags % 4, session_type=11 if flags & 8 else 6,
                              update_byte_11b68=flags & 1, update_client_state=(flags >> 1) & 1,
                              ethernet_address=0 if flags & 1 else 0x01020304, network_result=(flags >> 1) & 1,
                              rinse_pending=(flags >> 2) & 1, runtime_start_pending=1))
    for state in (12, 13, 14):
        for mask in range(64):
            cases.append(case(f"queue-priority-{state}-{mask}", state=state, queue_mask=mask, runtime_current=2 if state == 14 else 3,
                              temperature_enabled=mask & 1, service_mode=(mask >> 1) & 1))
    for event in (0, 1, 2, 7):
        for next_state in (0, 2, 3, 4, 18):
            cases.append(case(f"runtime-event-{event}-{next_state}", state=14, queue_mask=2, runtime_event=event, runtime_current=next_state))
    for event in (0, 1, 2):
        for next_state in (0, 15, 16, 17, 19):
            cases.append(case(f"still-event-{event}-{next_state}", queue_mask=32, still_event=event, still_state=next_state))
    for button in (0, 1, 255):
        cases.append(case(f"button-id-{button}", queue_mask=8, button_id=button))
    for state in range(18):
        for mode in range(4):
            for command in (0, 15, 16):
                cases.append(case(f"mode-{state}-{mode}-{command}", "mode", state=state, session_mode=mode, command_id=command))
    for mode in (0, 1, 2):
        for command in (15, 16):
            cases.append(case(f"still-mode-{mode}-{command}", "mode", state=13, session_type=11, session_mode=0,
                              config_still_mode=mode, command_id=command))
    for session_type in range(19):
        for flags in range(8):
            cases.append(case(f"start-{session_type}-{flags}", "start", session_type=session_type, runtime_start_pending=flags & 1,
                              rinse_pending=(flags >> 1) & 1, program_id=(flags >> 2) & 1))
    generator = random.Random(11920120)
    for index in range(160):
        values = dict(state=generator.choice([1, 4, 5, 7, 8, 9, 12, 13, 15]), now=generator.randrange(0x100000000),
                      state_date=generator.randrange(0x100000000), encoder_date=generator.randrange(0x100000000),
                      session_mode=generator.randrange(4), session_type=generator.randrange(17), msp_id=generator.choice([0, 2]),
                      runtime_id=generator.randrange(5), queue_mask=generator.choice([0, 0, 0, 1, 2, 8, 32]))
        for field in ("service_mode", "rinse_pending", "offline", "runtime_start_pending", "persist_credentials", "system_error", "msp_errors",
                      "sync_done", "backend_error", "registered", "update_available", "session_creation_error", "use_still_wifi", "config_ethernet"):
            values[field] = generator.randrange(2)
        cases.append(case(f"random-{index}", **values))
    return cases


def load_native(directory):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    library = Path(directory) / "core-flow.so"
    subprocess.run(compiler + FLAGS + ["-I", str(ROOT / "include"), str(ROOT / "tests/core_flow_bridge.cpp"),
                   str(ROOT / "src/core_flow.cpp"), str(ROOT / "src/eeprom.cpp"), "-o", str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.core_flow_action.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
                                       ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_int]
    native.core_flow_action.restype = ctypes.c_int
    return native, subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0]


def native_case(native, item, missing=False):
    words = (ctypes.c_uint32 * 64)(*item["words"])
    events = (ctypes.c_uint32 * 1024)()
    count = ctypes.c_size_t()
    credentials = ctypes.create_string_buffer(4 * 65)
    result = native.core_flow_action(KINDS.index(item["kind"]), words, events, ctypes.byref(count), credentials, missing)
    if count.value > len(events) or count.value % 4:
        raise AssertionError("Invalid Core trace shape")
    return result, {"id": item["id"], "words": list(words), "events": [list(events[i:i+4]) for i in range(0, count.value, 4)],
                    "credentials": [credentials.raw[i*65:(i+1)*65].split(b"\0", 1)[0].decode() for i in range(4)]}


def compare_case(native, item, expected):
    result, actual = native_case(native, item)
    if result != 0 or actual != expected:
        differences = [(key, got, want) for key, got, want in zip(WORDS, actual["words"], expected["words"]) if got != want]
        event_differences = [(i, got, want) for i, (got, want) in enumerate(zip(actual["events"], expected["events"])) if got != want]
        raise AssertionError(f"Core mismatch {item['id']}: {differences}; events {event_differences[:6]} counts {len(actual['events'])}/{len(expected['events'])}; credentials {actual['credentials']}/{expected['credentials']}")
    return len(actual["events"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-input", type=Path)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--original-jsonl", type=Path)
    parser.add_argument("--export", type=Path)
    parser.add_argument("--write-evidence", action="store_true")
    args = parser.parse_args()
    cases = json.loads(args.input.read_text()) if args.input else input_cases()
    if args.write_evidence and args.input:
        parser.error("Full deterministic input is required for evidence")
    if args.prepare_input:
        args.prepare_input.write_text(json.dumps(cases, separators=(",", ":")) + "\n")
        print(f"Prepared {len(cases)} Core scenarios")
    if not args.original_jsonl:
        if not args.prepare_input:
            parser.error("use --prepare-input or --original-jsonl")
        return 0
    if digest(ROOT.parent / "oem/upstream/zseries_0_0_119.bin") != IMAGE_SHA:
        raise SystemExit("Original image identity mismatch")
    execution_path = Path(str(args.original_jsonl) + ".json")
    execution = json.loads(execution_path.read_text())
    originals = [json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    if execution["input_sha256"] != IMAGE_SHA or execution["cases"] != len(cases) or len(originals) != len(cases):
        raise SystemExit("Unexpected original execution identity/count")
    events, goldens = 0, []
    with tempfile.TemporaryDirectory(prefix="zseries-core-flow-") as directory:
        native, compiler = load_native(directory)
        for index, (item, expected) in enumerate(zip(cases, originals)):
            events += compare_case(native, item, expected)
            if (item["id"].startswith(("state-field-", "deadline-", "tick-state-", "button-id-", "still-mode-", "transition-flags-")) or
                    index % 7 == 0 or item["id"] in ("queue-priority-12-63", "queue-priority-14-2")):
                goldens.append({"input": item, "original": expected})
    report = dict(execution, all_results_match=True, service_call_events=events, portable_golden_cases=len(goldens), native_compiler=compiler,
                  native_flags=FLAGS, original_execution_jsonl_sha256=digest(args.original_jsonl), original_execution_report_sha256=digest(execution_path))
    if args.write_evidence:
        if not args.export:
            parser.error("Evidence requires --export")
        report["instruction_export_sha256"] = {address: digest(args.export / "instructions" / ("fn_" + address[2:] + ".json"))
                                               for address in execution["original_functions_executed"]}
        report["source_sha256"] = {path: digest(ROOT / path) for path in ("include/zseries/core_flow.hpp", "src/core_flow.cpp", "tests/core_flow_bridge.cpp", "tests/core_flow_fixture.hpp",
                                   "tools/validate_core_flow.py", "tools/ghidra/EmulateCoreFlow.java", "tools/ghidra/Z119Execution.java")}
        fixture = ROOT / "tests/fixtures/core-flow-original.json"
        fixture.write_text(json.dumps({"schema_version": 1, "input_sha256": IMAGE_SHA, "words": WORDS, "cases": goldens}, separators=(",", ":")) + "\n")
        report["portable_golden_sha256"] = digest(fixture)
        (ROOT / "analysis/core-flow-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("cases", "all_results_match", "service_call_events", "portable_golden_cases")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
