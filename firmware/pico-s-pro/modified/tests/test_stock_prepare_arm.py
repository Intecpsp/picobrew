"""Run the production restore handoff in ARM emulation against a simulated SimpleLink filesystem."""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from rebuilt_arm import RebuiltArm

BOOT = b'/sys/mcubootinfo.bin'
APP = b'/sys/mcuimg2.bin'
RECOVERY = b'/sys/mcuimg1.bin'
WORKER = b'/sys/mcuimg3.bin'
NORMAL = bytes.fromhex('01 3d 00 20 ba dc cd ab')
UID = b'0123456789abcdef0123456789abcdef'
PROFILE_HASH = '7290674610eca1cf26581779a71c7f41a9c2fc367eb7189d0e93918319964bc7'


class RestoreReservation:
    def __init__(self, elf):
        self.arm = RebuiltArm(elf)
        self.arm.hook_symbol('_SlDrvCmdOp', self.dispatch)
        self.arm.write(self.arm.SCRATCH, UID + b'\0')
        self.files = {BOOT: NORMAL, APP: b'', RECOVERY: b''}
        self.alloc = {BOOT: 128, APP: 188416, RECOVERY: 65536}
        self.opened = None
        self.mutations = []
        self.create_error = self.info_error = 0
        self.corrupt = False

    def use_fixture_identity(self):
        # Keep private machine identity out of fixtures. Verify the production pin
        # before substituting the fixture hash in this emulator's RAM only.
        addr = self.arm.address('_ZZL20stock_layout_matchesPKcbPbE12profile_hash')
        assert self.arm.read(addr, 32).hex() == PROFILE_HASH
        self.arm.write(addr, hashlib.sha256(UID).digest())

    def prepare(self):
        return self.arm.call_symbol('_ZL13stock_preparePKc', [self.arm.SCRATCH])['r0']

    def dispatch(self, m):
        op = struct.unpack('<H', m.read(m.reg(0), 2))[0]
        msg, ext = m.reg(1), m.reg(2)
        if op in (0xA442, 0xA43C):
            name = m.cstring(m.read_u32(ext + 8))
            if op == 0xA442:  # FILEGETINFO
                err = self.info_error if name == WORKER and self.info_error else (0 if name in self.files else -11)
                m.write(msg, struct.pack('<hH6I', err, 0, len(self.files.get(name, b'')), self.alloc.get(name, 0), 0, 0, 0, 0))
                return 0
            mode = m.read_u32(msg)
            if mode:
                assert name == WORKER and name not in self.files
                self.mutations.append(('create', name))
                if self.create_error:
                    m.write(msg, struct.pack('<iI', self.create_error, 0))
                    return 0
                self.files[name], self.alloc[name] = b'', 49152
            assert self.opened is None and name in self.files
            self.opened = name
            m.write(msg, struct.pack('<iI', 47, 0))
        elif op == 0xA43D:  # FILECLOSE
            assert self.opened is not None
            self.opened = None
            m.write(msg, struct.pack('<hH', 0, 0))
        elif op in (0xA440, 0xA441):  # FILEREAD/FILEWRITE
            handle, offset, size, _ = struct.unpack('<IIHH', m.read(msg, 12))
            assert handle == 47 and self.opened is not None and offset == 0
            if op == 0xA440:
                data = self.files[self.opened][:size]
                m.write(m.read_u32(ext + 12), data)
            else:
                assert self.opened == WORKER
                data = m.read(m.read_u32(ext + 8), size)
                self.mutations.append(('write', WORKER))
                self.files[WORKER] = bytes(size) if self.corrupt else data
            m.write(msg, struct.pack('<hH', len(data), 0))
        else:
            raise AssertionError(f'Unexpected SimpleLink command {op:#x}')
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    args = parser.parse_args()
    b = RestoreReservation(args.elf)
    assert not b.prepare() and not b.mutations  # No per-unit evidence.
    b.use_fixture_identity()
    for selector in (0, 2, 255):
        b.files[BOOT] = bytes([selector]) + NORMAL[1:]
        assert not b.prepare() and not b.mutations
    b.files[BOOT] = bytes.fromhex('01 00 00 00 ba dc cd ab')
    assert not b.prepare() and not b.mutations
    b.files[BOOT] = NORMAL
    for name, limit in ((APP, 162792), (RECOVERY, 50504)):
        original = b.alloc[name]
        b.alloc[name] = limit - 1
        assert not b.prepare() and not b.mutations
        b.alloc[name] = original
    b.files[WORKER], b.alloc[WORKER] = b'Third-party image', 65536
    assert not b.prepare() and not b.mutations
    del b.files[WORKER]
    b.info_error = -5
    assert not b.prepare() and not b.mutations
    b.info_error, b.create_error = 0, -30
    assert not b.prepare() and WORKER not in b.files
    b.create_error = 0
    b.corrupt = True
    assert not b.prepare()
    before = list(b.mutations)
    assert not b.prepare() and b.mutations == before
    del b.files[WORKER]
    b.corrupt = False
    assert b.prepare()
    assert b.files[WORKER] == b'PicoBrew stock restore v1\0'
    before = list(b.mutations)
    assert b.prepare() and b.mutations == before
    b.alloc[WORKER] = 8192
    assert not b.prepare() and b.mutations == before
    assert b.files[BOOT] == NORMAL
    print('PASS: ARM stock restore reservation, identity, boot layout, capacity, readback and idempotent retry')


if __name__ == '__main__':
    main()
