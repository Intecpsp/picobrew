"""Check the 1.1.14 disassembly landmarks, without a network or a brewer."""
import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "modified/tools"))
import usb_stock


def verify(image):
    if not usb_stock.is_original_app(image):
        raise ValueError("Expected the complete, original 1.1.14 application")

    def at(address, expected):
        if image[address:address + len(expected)] != expected:
            raise ValueError(f"Unexpected bytes at flash byte {address:#x}")

    def ldi(reg, value):
        return struct.pack("<H", 0xe000 | (reg - 16) << 4 | (value & 0xf0) << 4 | (value & 15))

    def pair(reg, value):
        return ldi(reg, value & 255) + ldi(reg + 1, value >> 8)

    def direct_call(target):
        return bytes.fromhex("0e94") + struct.pack("<H", target // 2)

    for data_addr, sites in ((0x014d, [0x2eaa, 0x4d08, 0x9a82]),
                             (0x0886, [0xa734, 0x1032c, 0x1037c])):
        at(0x1b24a + data_addr - 0x100, b"picobrew.com\0")
        for site in sites:
            at(site, pair(22, data_addr))
    for reg, routine, sites in (
        (18, 0x11562, [0x42ae, 0x603c, 0x61a0, 0x6290, 0x63b6, 0x65e0,
                       0x671a, 0x6ae6, 0x6fc2, 0x87ee, 0x8e68, 0x901c, 0x98ca]),
        (20, 0x18580, [0xba10, 0xbf18, 0xc02a, 0xc228, 0xcae8, 0xcfbc,
                       0xd23a, 0xd3c8, 0xd836, 0xda36, 0xdd82, 0xdef6]),
    ):
        sequence = pair(reg, 80) + direct_call(routine)
        if image.count(sequence) != len(sites):
            raise ValueError("Unaccounted TCP connect sequence")
        for site in sites:
            at(site, sequence)
    for site in (0x6548, 0x6e18, 0x8776, 0x8f52, 0xb9c8, 0xbff6, 0xc15e, 0xd31c, 0xd99a, 0xdd02):
        at(site, direct_call(0x4342))
    for site, opcode in ((0x149e8, 0x1001), (0x14484, 0x1007), (0x1483c, 0x1008),
                         (0x1492c, 0x100b), (0x1454a, 0x1010), (0x14556, 0x1010)):
        at(site, pair(24, opcode))
    for site in (0x103dc, 0x107f4, 0x11024):
        at(site, direct_call(0x10252) + bytes.fromhex("80930b23"))
    for site, signature in {
            0x1056e: "10920b23", 0x19066: "5e988fe061e00e9400d0",
            0x19070: "8fe08ebd", 0x19226: "80ef8ebd",
            0x1909a: "5e9a8fe060e00e9400d0", 0x19250: "5e9a8fe060e00e9400d0",
            0x22a4: "04", 0x22c4: "80", 0x22e4: "07",
            0x185ae: "0e9430c8882321f0883111f08c3119f4",
            0x18b34: "c8010e9430c8882311f0883119f4",
    }.items():
        at(site, bytes.fromhex(signature))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("oem", type=Path, help="Original Zymatic_1_1_14.zip or extracted HEX")
    source = parser.parse_args().oem.read_bytes()
    verify(usb_stock.decode_application(usb_stock.unpack_source(source)))
    print("Verified network landmarks in Zymatic 1.1.14.")
