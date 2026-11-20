#!/usr/bin/env python3
"""Check complete C screen composition at its graphics/service boundaries.

All strings, row choices, menu labels, scrolling callbacks, IRQ order and
progress state are compared. Lower-level graphics has a separate full-byte
oracle; these tests do not establish appliance or physical equivalence.
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
from inspect_oem import VARIANTS

# Actual C entry points; parameter forms are reconstructed from caller/register use.
ENTRIES = [
    ("pico_ui_draw_deep_clean_confirmation", 0x20004c28, "none"),
    ("pico_ui_draw_descale_preparation", 0x20004c3c, "none"),
    ("pico_screen_water_error_help", 0x20004c64, "none"),
    ("pico_screen_thermal_recovery_help", 0x20004ca8, "none"),
    ("pico_ui_draw_descale_rinse_preparation", 0x20006154, "none"),
    ("pico_screen_still_setup_step", 0x20006808, "step"),
    ("pico_screen_ending_first_rinse", 0x2000a0f0, "none"),
    ("pico_screen_updating_firmware", 0x2000a810, "none"),
    ("pico_screen_downloading_firmware", 0x2000a84c, "none"),
    ("pico_screen_device_information", 0x2000b2e0, "none"),
    ("pico_ui_draw_done_footer", 0x2000b270, "none"),
    ("pico_ui_show_wifi_test_result", 0x2000e4c4, "stage"),
    ("pico_ui_draw_error", 0x20007214, "error"),
    ("pico_ui_wait_for_label_choice", 0x2000e63c, "text"),
    ("pico_screen_splash", 0x200056e4, "none"),
    ("pico_screen_wifi_searching", 0x20006dfc, "none"),
    ("pico_screen_ending_process", 0x20006ff4, "none"),
    ("pico_screen_show_program_notice", 0x20007030, "none"),
    ("pico_screen_preheating_steam", 0x200070d8, "int"),
    ("pico_screen_reinsert_step_filter", 0x2000719c, "none"),
    ("pico_screen_changing_step", 0x2000786c, "none"),
    ("pico_screen_setting_up_step", 0x200078b0, "text"),
    ("pico_screen_ending_step", 0x200078f0, "text"),
    ("pico_screen_loading", 0x200079c0, "int"),
    ("pico_screen_help_address", 0x20007ab4, "none"),
    ("pico_screen_show_help_temporarily", 0x20007b00, "none"),
    ("pico_screen_fill_reservoir_and_start", 0x20008a7c, "text"),
    ("pico_screen_starting_program", 0x20008ae4, "text"),
    ("pico_screen_ending_program", 0x20008b34, "text"),
    ("pico_screen_show_ending_program", 0x20008b84, "text"),
    ("pico_screen_service_status", 0x2000a1b0, "none"),
    ("pico_screen_wifi_no_signal", 0x2000a1f8, "none"),
    ("pico_screen_wifi_connected", 0x2000a244, "none"),
    ("pico_screen_wifi_connection_failed", 0x2000a280, "none"),
    ("pico_screen_error_heading", 0x2000b088, "none"),
    ("pico_ui_draw_language_menu", 0x20006fb8, "none"),
    ("pico_ui_draw_units_menu", 0x2000a2c8, "none"),
    ("pico_ui_draw_drain_filter", 0x20007160, "none"),
    ("pico_ui_draw_drain_reservoir", 0x200071d8, "none"),
    ("pico_ui_draw_choose_network", 0x20008560, "none"),
    ("pico_ui_draw_wifi_test_heading", 0x2000859c, "none"),
    ("pico_ui_draw_utilities_heading", 0x200086fc, "none"),
    ("pico_ui_draw_settings_heading", 0x20008738, "none"),
    ("pico_ui_draw_network_heading", 0x2000887c, "none"),
    ("pico_ui_draw_password_heading", 0x20008528, "none"),
    ("pico_ui_draw_firmware_reset_question", 0x20006f28, "none"),
    ("pico_ui_draw_clear_wifi_question", 0x20006f70, "none"),
    ("pico_ui_draw_skip_step_confirmation", 0x20007064, "none"),
    ("pico_ui_draw_stop_confirmation", 0x20008a14, "none"),
    ("pico_ui_draw_insert_picopak", 0x200077b0, "none"),
    ("pico_ui_draw_abv_adjustment", 0x200077fc, "none"),
    ("pico_ui_draw_bitterness_adjustment", 0x20007834, "none"),
    ("pico_ui_draw_picopak_selection", 0x20007990, "none"),
    ("pico_ui_draw_registration_code", 0x20007a30, "text"),
    ("pico_ui_draw_brew_complete", 0x20007930, "text"),
    ("pico_ui_draw_recipe_choices", 0x20008bf4, "choices"),
    ("pico_ui_draw_paused", 0x2000b1a0, "none"),
    ("pico_ui_draw_current_program_heading", 0x200083bc, "none"),
    ("pico_ui_draw_heater_malfunction", 0x200052ac, "none"),
    ("pico_ui_draw_stop_footer", 0x2000b2a4, "none"),
    ("pico_ui_draw_confirm_footer", 0x2000b2b8, "none"),
    ("pico_ui_draw_continue_footer", 0x2000b2cc, "none"),
    ("pico_ui_draw_ok_footer", 0x2000b0a4, "none"),
    ("pico_ui_draw_start_flush_footer", 0x2000b0b8, "none"),
    ("pico_ui_draw_start_transfer_footer", 0x2000b0e4, "none"),
    ("pico_ui_draw_start_rinse_footer", 0x2000b218, "none"),
    ("pico_ui_draw_start_clean_footer", 0x2000b244, "none"),
    ("pico_ui_draw_instruction_screen", 0x20007d20, "instruction"),
]


def verify(variant, native):
    original = OriginalC(variant)
    delta = -0x18 if variant == "ssd1309" else 0
    early = 4 if variant == "ssd1309" else 0
    events, config = [], []
    labels, scrolling, program = 0x2002a6cc, 0x2002c2c0, 0x2002a8b0
    def event(kind, a=0, b=0, text=b""):
        if len(events) >= 128:
            raise AssertionError(("Unexpected unbounded UI trace", events[:16]))
        events.append([kind, a & 0xffffffff, b & 0xffffffff, text.hex()])
    def start(r):
        cb = r.reg(2) & ~1
        ids = {0x2000b2a4 + delta: 1, 0x2000b2b8 + delta: 2, 0x2000b2cc + delta: 3}
        event(6, ids.get(cb, 0), 0, r.cstring(r.reg(1)))
        original.write(scrolling + 0x130, b"\1")
    mapping = json.loads((ROOT / "analysis/function-correspondence.json").read_text())
    candidates = mapping["variants"][VARIANTS[variant]["display"]]["comparison"]["candidates"]
    addresses = {x["name"]: int(x["target"], 16) for x in candidates}
    original.hook(addresses["pico_assert_nonfatal"], lambda r: None)
    original.hook(0x20004488, lambda r: event(1))
    original.hook(0x20004494, lambda r: event(2))
    original.hook(0x200054ac + (-0x14 if variant == "ssd1309" else 0), lambda r: event(3, r.reg(0), 0, r.cstring(r.reg(1))))
    original.hook(0x20005474 + (-0x14 if variant == "ssd1309" else 0), lambda r: event(4, r.reg(0), r.reg(1), r.cstring(r.reg(2))))
    original.hook(0x20009944 + delta, lambda r: event(5, r.reg(0)))
    original.hook(0x20004bd0 + early, start)
    original.hook(0x2000452c, lambda r: event(7, r.reg(0), 0, r.cstring(r.reg(1))))
    original.hook(0x20018922 + (-0xa4 if variant == "ssd1309" else 0), lambda r: event(8))
    original.hook(0x20018918 + (-0xa4 if variant == "ssd1309" else 0), lambda r: (event(9), 0)[1])
    original.hook(0x200050b8 + early, lambda r: (event(10, config[2]), config[2])[1])
    original.hook(0x20017d4c + (-0xa4 if variant == "ssd1309" else 0), lambda r: event(11, r.reg(0)))
    original.hook(0x2000b080 + delta, lambda r: event(12, 0, 0, r.cstring(r.reg(0))))
    original.hook(0x20004adc + early,
                  lambda r: event(13, 0, 0, b"|".join(r.cstring(r.reg(i)) for i in range(3))))
    original.hook(0x20009a54 + delta, lambda r: event(14))
    original.hook(addresses["pico_menu_choose"], lambda r: (event(16, r.reg(0), r.reg(1)), r.reg(2))[1])
    original.hook(0x2000e394 + delta, lambda r: event(17))
    def mac(r):
        event(18)
        original.write(r.reg(1), bytes.fromhex("021a00ff08ab"))
    original.hook(addresses["WiFi_get_mac_address"], mac)
    original.hook(0x2000b284 + delta, lambda r: event(19, 0, 0, r.cstring(r.reg(0))))
    traces, original_faults = [], []
    for name, entry, form in ENTRIES:
        for language in (0, 1, 2, 255):
            for alternate in (0, 1):
                values = {"instruction": range(25), "stage": range(8), "step": range(4),
                          "error": [*range(20), 24, 99, 255]}.get(form, [alternate])
                for value in values:
                    config[:] = [language, alternate, alternate, alternate]
                    native.pico_test_screen_reset(*config)
                    original.write(0x2002a90c, bytes([language]))
                    original.write(0x2002c127, bytes([alternate]))
                    original.write(0x2002bc68, b"Recipe\0")
                    original.write(0x2002c255, b"PICO-001\0")
                    original.write(labels, b"Z" * 484)
                    original.write(0x2002a6b8, bytes(20))
                    original.write(scrolling + 0x130, bytes([alternate]))
                    original.write_u32(0x2002a480, program)
                    original.write_u32(program + 0x24, 149)
                    original.write_u32(program + 0x28, 3661)
                    for offset, address, text in ((0x38, original.SCRATCH + 0x1000, b"Recipe"),
                                                  (0x3c, original.SCRATCH + 0x1100, b"Brewing"),
                                                  (0x44, original.SCRATCH + 0x1200, b"Paused session")):
                        original.write(address, text + b"\0")
                        original.write_u32(program + offset, address)
                    text = b"Recipe X"
                    original.write(original.SCRATCH, text + b"\0")
                    original.write(original.SCRATCH + 0x200, bytes([value]))
                    args = {"none": [], "int": [value], "text": [original.SCRATCH],
                            "choices": [original.SCRATCH, value],
                            "instruction": [value, language, original.SCRATCH],
                            "step": [original.SCRATCH + 0x200], "stage": [value], "error": [value]}[form]
                    step = ctypes.c_uint8(value)
                    native_args = {"none": [], "int": [value], "text": [ctypes.c_char_p(text)],
                                   "choices": [ctypes.c_char_p(text), value],
                                   "instruction": [value, language, ctypes.c_char_p(text), 0],
                                   "step": [ctypes.byref(step)], "stage": [value], "error": [value]}[form]
                    events.clear()
                    try:
                        original.call(entry + (early if entry <= 0x200052ac else delta), args, max_instructions=200000)
                    except RuntimeError as error:
                        if (name == "pico_ui_draw_clear_wifi_question" and language == 2
                                and original.reg("pc") == 0x73656576):
                            # The string is emitted before the OEM returns through
                            # bytes from "archivees". Compare effects up to that
                            # observed defect; do not call its return equivalent.
                            original_faults.append({"function": name, "language": language,
                                                    "return_pc": "0x73656576"})
                        else:
                            raise AssertionError((variant, name, config, value, events)) from error
                    getattr(native, name)(*native_args)
                    actual_events = [[native.pico_test_screen_event(i, j) for j in range(3)] +
                                     [native.pico_test_screen_text(i).hex()]
                                     for i in range(native.pico_test_screen_events())]
                    expected_state = [original.read(scrolling + 0x130, 1)[0], *original.read(0x2002a6b8, 4),
                                      original.read(0x2002a6c8, 1)[0]]
                    actual_state = [native.pico_test_screen_state(i) for i in range(6)]
                    if events != actual_events or expected_state != actual_state:
                        raise AssertionError((variant, name, config, value, events, actual_events, expected_state, actual_state))
                    if original.read(labels, 484) != ctypes.string_at(native.pico_test_screen_labels(), 484):
                        raise AssertionError((variant, name, config, "menu labels differ"))
                    traces.append([name, config[:], value, events[:], expected_state])
    return {"variant": variant, "input_sha256": VARIANTS[variant]["raw_sha256"], "cases": len(traces),
            "functions": len(ENTRIES), "trace_sha256": hashlib.sha256(json.dumps(traces).encode()).hexdigest(),
            "original_code_executed": True, "native_source_compiled": True, "hardware_tested": False,
            "original_stack_overflows_observed": original_faults,
            "limits": ["Graphics and peripheral boundaries recorded, not physical hardware",
                       "Menu/scroll/progress state checked; complete appliance not executed",
                       "French clear-WiFi prompt compared until observed OEM stack-overflow return; typed source bounds repaired"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pico-c-screens-") as directory:
        native = compile_library(Path(directory), "ui_screens", ["src/ui/screens.c", "src/ui/ui_screens.c",
            "src/ui/instruction_screens.c", "src/ui/assets.c", "src/ui/translations.c", "src/ui/strings.c",
            "src/ui/scrolling_prompt.c",
            "tests/ui_screen_fixture.c"])
        native.pico_test_screen_event.restype = ctypes.c_uint32
        native.pico_test_screen_text.restype = ctypes.c_char_p
        native.pico_test_screen_labels.restype = ctypes.c_void_p
        reports = [verify(variant, native) for variant in VARIANTS]
    content = json.dumps({"cases": sum(r["cases"] for r in reports), "variants": reports}, indent=2) + "\n"
    if args.output:
        args.output.write_text(content)
    print(content, end="")


if __name__ == "__main__":
    main()
