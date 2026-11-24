"""Link the entire experimental radio into the extended board's real flash windows."""

from pathlib import Path
import shutil
import subprocess
import sys

# Ordinals match the inspected network, display and keyboard regions returned
# by their ROM composers. Preserve each function's natural AVR instruction alignment.
SECTIONS = [
    ("encoder", ("zym_spi_submit", "zymatic_history_row")),
    ("wifi_fields", ("zymatic_api_reply_byte", "zymatic_history_ready")),
    ("wired_fields", ("zymatic_api_more", "zym_spi_request_display")),
    ("oled_transfer", ("step_index",)),
    ("oled_clear", ("number", "submit", "zym_spi_bind")),
    ("oled_init", ("poll_network", "zymatic_image_valid", "zymatic_temperature_tenths")),
    ("key_one", ("reg_io",)),
    ("key_two", ("bind_from", "zymatic_factory_matches")),
]
ENTRIES = (
    "candidate_radio", "candidate_broker", "zymatic_wifi_server_poll", "zym_broker_begin", "zym_broker_irq",
    "zym_broker_acquire_oem", "zym_broker_release_oem", "zym_broker_link_lost", "zym_cc3000_begin",
    "zym_spi_bind", "zym_spi_record_irq", "zym_spi_receive", "zym_spi_submit", "zym_spi_release_frame",
    "zym_spi_stock_restart", "zym_spi_yield_to_stock", "zym_spi_resume_from_stock", "zym_spi_flush_pending",
    "zym_spi_request_display",
    "zym_cc3000_board_begin", "zym_cc3000_board_poll",
)


def link(root, runtime, toolchain, windows, original):
    wifi = Path(__file__).resolve().parents[1]
    src = runtime / "src"
    for path in sorted(wifi.glob("*.[ch]")):
        # Match the actual API's typed flash credentials on both transports.
        source = path.read_text().replace("const uint8_t key[32]", "ZymKey key").replace("const char *uid", "ZymUid uid")
        (src / path.name).write_text(source)
    shutil.copyfile(wifi / "layout/state.c", src / "candidate_state.c")
    linker = src / "overlay.ld"
    script = linker.read_text()
    for (name, functions), (start, end) in zip(SECTIONS, windows, strict=True):
        script = script.replace("MEMORY {", f"MEMORY {{\n  {name} (rx) : ORIGIN = {start:#x}, LENGTH = {end-start:#x}")
        selectors = " ".join(f"*(.text.{func})" for func in functions)
        script = script.replace("SECTIONS {", f"SECTIONS {{\n  .{name} : {{ {selectors} }} > {name}")
    script = script.replace("*(.low_functions)", "*(.low_functions) *(.text.zymatic_history_poll) *(.text.history_value)")
    linker.write_text(script)
    compiler = runtime / "tools/compile_extension.py"
    source = compiler.read_text()
    for old, new in {
        "entry_points = [": f"entry_points = [{', '.join(map(repr, ENTRIES))}, ",
        '"-Wl,--gc-sections",': '"-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",',
        'invoke("avr-objcopy", "-j", ".text",':
            f'shutil.copyfile(work / "extension.elf", {str(root / "linked.elf")!r})\n        invoke("avr-objcopy", "-j", ".text",',
    }.items():
        if source.count(old) != 1:
            raise ValueError("The base AVR compiler interface changed")
        source = source.replace(old, new)
    compiler.write_text(source)
    subprocess.run([sys.executable, compiler, "--toolchain", toolchain, "--out-dir", runtime / "extension"], check=True)

    packed = bytearray(original)
    for (name, _), (start, end) in zip(SECTIONS, windows, strict=True):
        path = root / (name + ".bin")
        subprocess.run([toolchain / "avr-objcopy", "-j", "." + name, "-O", "binary", root / "linked.elf", path], check=True)
        code = path.read_bytes()
        if not 0 < len(code) <= end - start or packed[start:end] != b"\xff" * (end-start):
            raise ValueError("AVR code overlaps an occupied ROM window " + name)
        packed[start:start+len(code)] = code
    return bytes(packed)
