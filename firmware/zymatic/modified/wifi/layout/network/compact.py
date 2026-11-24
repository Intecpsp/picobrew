"""Measure two replacements for the stock network routines using the original 1.1.14 ROM."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / "tools"))
import usb_stock


def compose(original, toolchain, directory):
    if not usb_stock.is_original_app(original):
        raise ValueError("Expected the recognized Zymatic 1.1.14 application")
    candidate = bytearray(original)

    def build(name, source, *linker_options):
        elf = directory / (name + ".elf")
        binary = directory / (name + ".bin")
        subprocess.run([toolchain / "avr-gcc", "-Os", "-mmcu=atmega1284p", "-nostdlib",
                        *linker_options, HERE / source, "-o", elf], check=True)
        subprocess.run([toolchain / "avr-objcopy", "-j", ".text", "-O", "binary", elf, binary], check=True)
        symbols = {}
        for line in subprocess.check_output([toolchain / "avr-nm", elf], text=True).splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[1] in ("t", "T"):
                symbols[parts[2]] = int(parts[0], 16)
        return binary.read_bytes(), symbols

    encoder, enc = build("encoder", "escape.c", "-Wl,-e,encode", "-Wl,-T," + str(HERE / "escape.ld"))
    reader, rd = build("reader", "read_fields.S", "-Wl,-e,ethernet,-Ttext=0xccba")
    if enc["encode"] != 0x2ed0 or rd["ethernet"] != 0xccba:
        raise ValueError("Original entry points must be preserved")
    if original[0x341a:0x35dc] != original[0xcd34:0xcef6]:
        raise ValueError("Original network parsers no longer share their field conversion code")

    def branch(site, expected, target, call=False):
        if original[site:site + 4] != bytes.fromhex(expected) or target & 1 or target >= 0x20000:
            raise ValueError(f"Unexpected original instruction or target at {site:#x}")
        candidate[site:site + 4] = bytes([0x0e if call else 0x0c, 0x94]) + (target // 2).to_bytes(2, "little")

    branch(0x33a0, "2f923f92", rd["wifi"])
    branch(0x3402, "0e944019", rd["fetch"], call=True)
    branch(0x35e0, "81e0f501", rd["success"])
    branch(0x361e, "2f900895", rd["cleanup"])
    ranges = []
    for start, end, code in ((0x2ed0, 0x3228, encoder), (0xccba, 0xcf38, reader)):
        if len(code) > end - start:
            raise ValueError("Replacement exceeds its original region")
        candidate[start:end] = code + b"\xff" * (end - start - len(code))
        ranges.append((start + len(code), end))
    return bytes(candidate), ranges


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oem-file", required=True, type=Path)
    parser.add_argument("--toolchain", required=True, type=Path)
    args = parser.parse_args()
    original = usb_stock.decode_application(usb_stock.unpack_source(args.oem_file.read_bytes()))
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        candidate, ranges = compose(original, args.toolchain.resolve(), directory)
        stock, test = directory / "original.bin", directory / "test.bin"
        stock.write_bytes(original)
        test.write_bytes(candidate)
        subprocess.run(["node", HERE / "compare.cjs", stock, test], check=True)
        for start, end in ranges:
            print(f"[{start:#x}, {end:#x}) {end - start} bytes")


if __name__ == "__main__":
    main()
