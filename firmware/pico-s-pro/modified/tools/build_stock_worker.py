"""Build or verify the independent S/Pro slot-three stock restore program."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
RECOVERY = ROOT / "recovery"
SOURCE = ROOT / "platform/stock_restore"
PACKAGE = ROOT / "firmware/standalone/pico-stock-restore.bin"
MARKER = bytes.fromhex("226cab39b4ce60a577c0841941c1e976e075369ab730a0c5b7a19295d08c620d")
MAXIMUM = 49152


def seal(elf_file, raw_file):
    raw = bytearray(raw_file.read_bytes())
    elf_bytes = bytearray(elf_file.read_bytes())
    with elf_file.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s for s in elf.get_section_by_name(".symtab").iter_symbols()
                   if s.name and s["st_shndx"] != "SHN_UNDEF"}
        symbol = symbols["stock_worker_seal"]
        section = elf.get_section(symbol["st_shndx"])
        seal_offset = symbol["st_value"] - 0x20004000
        file_offset = section["sh_offset"] + symbol["st_value"] - section["sh_addr"]
        stack, reset = struct.unpack_from("<II", raw)
        if (elf["e_machine"] != "EM_ARM" or not elf.little_endian or
                symbols["g_pfnVectors"]["st_value"] != 0x20004000 or
                (reset | 1) != (symbols["ResetISR"]["st_value"] | 1) or not reset & 1 or
                stack != symbols["pui32Stack"]["st_value"] + 32768 or stack & 7 or
                symbols["_eheap"]["st_value"] - symbols["_heap"]["st_value"] != 32768 or
                symbols["_eheap"]["st_value"] > 0x20040000 or len(raw) > MAXIMUM or
                symbols["_edata"]["st_value"] != 0x20004000 + len(raw)):
            raise ValueError("Stock worker exceeds the qualified ARM layout")
        for name in symbols:
            if (name.startswith(("pico_recipe_", "pico_actuator_", "pico_recovery_select",
                                 "pico_application_", "pico_temperature_control_", "unused_recovery_")) or
                    name in {"sl_FsFormat", "sl_WlanProfileDel"}):
                raise ValueError("Unexpected worker symbol: " + name)
        if symbol["st_size"] != 32 or raw[seal_offset:seal_offset+32] != MARKER or elf_bytes[file_offset:file_offset+32] != MARKER:
            raise ValueError("Invalid worker digest field")
    raw[seal_offset:seal_offset+32] = bytes(32)
    digest = hashlib.sha256(raw).digest()
    raw[seal_offset:seal_offset+32] = digest
    elf_bytes[file_offset:file_offset+32] = digest
    elf_file.write_bytes(elf_bytes)
    raw_file.write_bytes(raw)
    return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--check", action="store_true")
    group.add_argument("--package", action="store_true")
    args = parser.parse_args()
    out = ROOT / "build/stock-restore"
    prefix = os.environ.get("PICOBREW_ARM_PREFIX", str(ROOT / ".deps/toolchain/bin/arm-none-eabi-"))
    cmd = [sys.executable, ROOT / "tools/build_sdk_validation.py", "--stock-worker",
           "--entry", SOURCE / "native.cpp", "--sdk-root", os.environ.get("PICOBREW_SDK", ROOT / ".deps/energia"),
           "--arm-prefix", prefix, "--output-dir", out, "--linker", RECOVERY / "platform/pico_cc3200.ld",
           "--include", RECOVERY / "recovery"]
    cmd.extend(["--include", RECOVERY / "include"])
    for src in [SOURCE / "stock.c", SOURCE / "http.c", ROOT / "platform/hub_recovery/identity.cpp",
                RECOVERY / "recovery/sha256.c", RECOVERY / "src/http_server.c"]:
        cmd.extend(["--source", src])
    subprocess.run(cmd, check=True)
    raw = seal(out / "sdk_validation.elf", out / "sdk_validation.bin")
    if args.package:
        PACKAGE.write_bytes(raw)
    if args.check:
        if raw != PACKAGE.read_bytes():
            raise ValueError("Packaged stock worker differs from the selected source")
        subprocess.run([sys.executable, ROOT / "tests/test_stock_worker_arm.py", out / "sdk_validation.elf"], check=True)
    print(f"Stock restore worker: {len(raw)} bytes, SHA-256 {hashlib.sha256(raw).hexdigest()}")


if __name__ == "__main__":
    main()
