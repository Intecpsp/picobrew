"""Execute the packaged ARM restore worker's slot checks with a fake SimpleLink filesystem."""
from pathlib import Path
import struct
import sys

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_XPSR)

ROOT = Path(__file__).resolve().parents[1]
APP = "/sys/mcuimg2.bin"
RECOVERY = "/sys/mcuimg1.bin"
WORKER = "/sys/mcuimg3.bin"
BOOT = "/sys/mcubootinfo.bin"
BOOT_STOCK = bytes.fromhex("013d0020badccdab")


class Worker:
    def __init__(self, elf_path, binary):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0x20000000, 1024*1024)
        self.uc.mem_write(0x20004000, binary)  # run the packaged bytes, ELF supplies names only
        with elf_path.open("rb") as f:
            table = ELFFile(f).get_section_by_name(".symtab")
            self.symbols = {s.name: s["st_value"] for s in table.iter_symbols()}
        driver = self.symbols["_SlDrvCmdOp"] & ~1
        self.uc.hook_add(UC_HOOK_CODE, self.command, begin=driver, end=driver)
        self.uc.mem_write(self.symbols["_ZN12_GLOBAL__N_111nwp_startedE"], b"\x01")
        original = ROOT.parent / "oem/device"
        self.files = {APP: (original / "mcuimg2.bin").read_bytes(),
                      RECOVERY: (original / "mcuimg1.bin").read_bytes(),
                      WORKER: binary, BOOT: bytes([2]) + BOOT_STOCK[1:]}
        self.capacity = {APP: 194560, RECOVERY: 61440, WORKER: 49152, BOOT: 256}
        self.active = {}
        self.next_handle = 20
        self.deletions = []

    def word(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def command(self, uc, address, size, _):
        opcode = struct.unpack("<H", uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 2))[0]
        message, ext = uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R2)
        if opcode in (0xA43C, 0xA442, 0xA443):
            path = bytes(uc.mem_read(self.word(ext+8), 64)).partition(b"\0")[0].decode()
        if opcode == 0xA442:
            present = path in self.files
            uc.mem_write(message, struct.pack("<hH6I", 0 if present else -11, 0,
                         len(self.files.get(path, b"")), self.capacity.get(path, 0), 0, 0, 0, 0))
        elif opcode == 0xA43C:
            mode = self.word(message)
            assert mode in (0, 0x1000) and path in self.files
            if mode:
                assert path == BOOT
                self.files[path] = b""
            handle = self.next_handle
            self.next_handle += 1
            self.active[handle] = path
            uc.mem_write(message, struct.pack("<iI", handle, 0))
        elif opcode == 0xA43D:
            del self.active[self.word(message)]
            uc.mem_write(message, struct.pack("<hH", 0, 0))
        elif opcode in (0xA440, 0xA441):
            handle, offset, length, _ = struct.unpack("<IIHH", uc.mem_read(message, 12))
            path = self.active[handle]
            if opcode == 0xA440:
                data = self.files[path][offset:offset+length]
                uc.mem_write(self.word(ext+12), data)
                length = len(data)
            else:
                assert path == BOOT and offset == len(self.files[path])
                self.files[path] += bytes(uc.mem_read(self.word(ext+8), length))
            uc.mem_write(message, struct.pack("<hH", length, 0))
        elif opcode == 0xA443:
            assert path == WORKER and self.files[BOOT] == BOOT_STOCK
            self.deletions.append(path)
            del self.files[path]
            uc.mem_write(message, struct.pack("<hH", 0, 0))
        else:
            raise AssertionError(f"Unexpected driver command {opcode:#x}")
        uc.reg_write(UC_ARM_REG_R0, 0)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def call(self, name, arg=0):
        return_pc = 0x200ff000
        self.uc.reg_write(UC_ARM_REG_R0, arg)
        self.uc.reg_write(UC_ARM_REG_SP, 0x200f0000)
        self.uc.reg_write(UC_ARM_REG_LR, return_pc | 1)
        self.uc.reg_write(UC_ARM_REG_XPSR, 1 << 24)
        self.uc.emu_start(self.symbols["_ZN12_GLOBAL__N_1" + name] | 1, return_pc, count=80000000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == return_pc and not self.active
        return self.uc.reg_read(UC_ARM_REG_R0)


def main():
    elf = Path(sys.argv[1]) if len(sys.argv) == 2 else ROOT / "build/stock-restore/sdk_validation.elf"
    program = (ROOT / "firmware/standalone/pico-stock-restore.bin").read_bytes()
    w = Worker(elf, program)
    assert w.call("10own_workerEv") == 1
    assert w.call("7matchesE11StockTarget", 0) == 1
    assert w.call("7matchesE11StockTarget", 1) == 1
    w.files[APP] = b"\xff" + w.files[APP][1:]
    assert w.call("7matchesE11StockTarget", 0) == 0
    assert w.call("13remove_workerEv") == 0 and WORKER in w.files
    original_boot = w.files[BOOT]
    w.files[BOOT] = original_boot[:3] + b"\x01" + original_boot[4:]
    assert w.call("12select_stockEv") == 0 and WORKER in w.files
    w.files[BOOT] = original_boot
    assert w.call("12select_stockEv") == 1 and w.files[BOOT] == BOOT_STOCK
    for offset in (0, w.symbols["stock_worker_seal"]-0x20004000, len(program)-1):
        damaged = bytearray(program)
        damaged[offset] ^= 1
        w.files[WORKER] = bytes(damaged)
        assert w.call("13remove_workerEv") == 0 and not w.deletions
    w.files[WORKER] = program
    assert w.call("13remove_workerEv") == 1 and w.deletions == [WORKER]
    assert w.call("13remove_workerEv") == 1 and WORKER not in w.files
    print("Packaged ARM worker verified the OEM bytes, read back the boot record and removed only its own image.")


if __name__ == "__main__":
    main()
