#!/usr/bin/env python3
"""Check C action tables and localized menu builders against both ARM images.

The OEM table bounds error at action 50 is deliberately excluded: the typed
source rejects it instead of returning adjacent diagnostic data as a pointer.
Original code runs with only nonfatal assertion/log boundaries intercepted.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

from test_control import OriginalC, compile_library

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_oem import read_variant, VARIANTS

MENUS = {
    "pico_menu_populate_still_fan": 0x20004884,
    "pico_menu_populate_hardware_tests": 0x20006c98,
    "pico_menu_populate_service": 0x200082c0,
    "pico_menu_populate_network": 0x20008438,
    "pico_menu_populate_settings": 0x20008774,
    "pico_menu_populate_languages": 0x200088b8,
    "pico_menu_populate_post_brew": 0x20008c6c,
}
LABELS = {
    "pico_label_done": 0x20008210, "pico_label_confirm": 0x2000823c,
    "pico_label_continue": 0x20008268, "pico_label_exit": 0x20008294,
    "pico_label_back": 0x2000840c, "pico_label_ok": 0x200084c4,
    "pico_label_yes": 0x200084e0, "pico_label_no": 0x2000850c,
    "pico_label_compartment": 0x200085c8, "pico_label_service": 0x2000883c,
    "pico_label_heating": 0x20008954, "pico_label_cooling": 0x20008980,
    "pico_label_holding": 0x200089ac, "pico_label_draining": 0x200089d8,
    "pico_label_start_brewing": 0x20008b9c, "pico_label_cancel": 0x20008bc8,
    "pico_label_next_uppercase": 0x20008cd4, "pico_label_exit_uppercase": 0x20008d00,
}


def verify(variant, native):
    original = OriginalC(variant)
    image = read_variant(variant)
    delta = -0x18 if variant == "ssd1309" else 0
    early_delta = 4 if variant == "ssd1309" else 0
    def literal(address):
        return struct.unpack_from("<I", image, address - 0x20004000)[0]
    labels = literal(0x200088ec + delta)
    language_address = literal(0x20008428 + delta)
    # The first BL at each action entry is the nonfatal assertion boundary.
    report = json.loads((ROOT / "analysis/function-correspondence.json").read_text())
    names = report["variants"][VARIANTS[variant]["display"]]["comparison"]["candidates"]
    addresses = {entry["name"]: int(entry["target"], 16) for entry in names}
    original.hook(addresses["pico_assert_nonfatal"], lambda runner: None)
    original.hook(0x20004adc + early_delta, lambda runner: None)
    cases = []
    def reset(language):
        original.write(language_address, bytes([language]))
        original.write(labels, b"Z" * 484)
        native.pico_test_data_reset(language)
    for action in range(50):
        expected = original.cstring(original.call(0x20004400, [action])["r0"])
        actual = native.pico_action_name(action)
        if expected != actual:
            raise AssertionError((variant, "action name", action, expected, actual))
        cases.append(expected.hex())
        for language in range(3):
            for status in (0, 1, 255):
                expected = original.cstring(original.call(0x2000442c, [action, language, status])["r0"])
                actual = native.pico_action_label(action, language, status)
                if expected != actual:
                    raise AssertionError((variant, "action label", action, language, status, expected, actual))
                cases.append(expected.hex())
    for language in (0, 1, 2, 255):
        for name, main_address in LABELS.items():
            for compartment in (range(10) if name == "pico_label_compartment" else [0]):
                reset(language)
                original.write(original.SCRATCH, b"Z" * 80)
                target = ctypes.create_string_buffer(b"Z" * 80, 80)
                original.call(main_address + delta, [original.SCRATCH, compartment])
                getattr(native, name)(target, compartment)
                expected = original.read(original.SCRATCH, 80)
                if expected != target.raw:
                    raise AssertionError((variant, name, language, compartment, expected, target.raw))
                cases.append(expected.hex())
        for name, main_address in MENUS.items():
            reset(language)
            address = main_address + (early_delta if main_address < 0x20005440 else delta)
            expected_return = original.call(address)["r0"]
            actual_return = getattr(native, name)()
            expected = original.read(labels, 484)
            actual = ctypes.string_at(native.pico_test_data_labels(), 484)
            if expected != actual or expected_return != actual_return:
                raise AssertionError((variant, name, language, expected_return, actual_return,
                                      [expected[i:i+22] for i in range(0, 154, 22)],
                                      [actual[i:i+22] for i in range(0, 154, 22)]))
            cases.append(expected.hex())
        for start in (0, 2, 17):
            reset(language)
            original.write_u32(original.SCRATCH, start)
            count = ctypes.c_int(start)
            original.call(0x20006e40 + delta, [original.SCRATCH])
            native.pico_menu_append_wifi_search_actions(ctypes.byref(count))
            expected = original.read(labels, 484)
            if expected != ctypes.string_at(native.pico_test_data_labels(), 484) or count.value != original.read_u32(original.SCRATCH):
                raise AssertionError((variant, "WiFi choices", language, start))
            cases.append(expected.hex())
    return {"variant": variant, "input_sha256": VARIANTS[variant]["raw_sha256"],
            "cases": len(cases), "result_sha256": hashlib.sha256(json.dumps(cases).encode()).hexdigest(),
            "original_code_executed": True, "native_source_compiled": True,
            "hardware_tested": False,
            "limits": ["Valid action IDs 0..49 only; action 50 OEM out-of-bounds defect repaired",
                       "Assertion/log side effects excluded; string and menu bytes compared"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pico-c-ui-data-") as directory:
        native = compile_library(Path(directory), "ui_data", [
            "src/ui/actions.c", "src/ui/action_tables.c", "src/ui/menus.c",
            "src/ui/translations.c", "src/ui/strings.c", "src/network/text.c",
            "tests/ui_data_fixture.c"])
        native.pico_action_name.restype = ctypes.c_char_p
        native.pico_action_label.restype = ctypes.c_char_p
        native.pico_test_data_labels.restype = ctypes.c_void_p
        reports = [verify(variant, native) for variant in VARIANTS]
    result = {"cases": sum(report["cases"] for report in reports), "variants": reports}
    content = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(content)
    print(content, end="")


if __name__ == "__main__":
    main()
