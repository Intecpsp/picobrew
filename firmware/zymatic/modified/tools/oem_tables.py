"""Losslessly pack the two fixed 1.1.14 data regions. AVR addresses are byte addresses."""
from collections import Counter
from functools import lru_cache
import hashlib
import struct

# Nothing outside this table moves. Its original address is the public lookup key.
POOL = 0x0254
POOL_END = 0x1c77
BLOCK = 128
ESCAPE = 127
PACKED_END = 0x12f2
PACKED_SHA256 = '96167d7fae0518a6e3368bb9d615eed4defcb2a996e743ece584b08c4f6e2ac1'
CRT = 0x1b24a
CRT_SIZE = 0x980
HIGH_START = 0x1b920

@lru_cache(maxsize=2)
def pack_pool(data):
    if len(data) != POOL_END - POOL:
        raise ValueError("Incorrect OEM low-flash pool size")
    streams = [[b if b < ESCAPE else (ESCAPE, b) for b in data[p:p+BLOCK]]
               for p in range(0, len(data), BLOCK)]
    dictionary = bytearray()
    lengths = [1] * 256
    for token in range(128, 256):
        counts = Counter((left, right) for stream in streams for left, right in zip(stream, stream[1:])
                         if isinstance(left, int) and isinstance(right, int))
        (left, right), count = counts.most_common(1)[0]
        if count <= 3:
            raise ValueError("Unexpected OEM dictionary")
        lengths[token] = lengths[left] + lengths[right]
        dictionary.extend((left, right, lengths[token]))
        for stream in streams:
            i = 0
            replaced = []
            while i < len(stream):
                if i+1 < len(stream) and stream[i] == left and stream[i+1] == right:
                    replaced.append(token)
                    i += 2
                else:
                    replaced.append(stream[i])
                    i += 1
            stream[:] = replaced
    offsets = bytearray()
    payload = bytearray()
    for stream in streams:
        offsets.extend(struct.pack('<H', len(payload)))
        for token in stream:
            payload.extend((token,) if isinstance(token, int) else token)
    packed = bytes(dictionary + offsets + payload)
    if unpack_pool(packed) != data:
        raise ValueError("Lossy low-flash table")
    return packed

@lru_cache(maxsize=2)
def unpack_pool(packed):
    if len(packed) != PACKED_END - POOL or hashlib.sha256(packed).hexdigest() != PACKED_SHA256:
        raise ValueError("Unrecognized OEM text/bitmap dictionary")
    phrases = [bytes([i]) for i in range(128)]
    for pos in range(0, 384, 3):
        left, right, size = packed[pos:pos+3]
        phrase = phrases[left] + phrases[right]
        if len(phrase) != size or size > BLOCK:
            raise ValueError("Invalid dictionary phrase")
        phrases.append(phrase)
    count = (POOL_END - POOL + BLOCK - 1) // BLOCK
    payload = packed[384+2*count:]
    starts = list(struct.unpack_from('<' + 'H'*count, packed, 384)) + [len(payload)]
    raw = bytearray()
    for start, end in zip(starts, starts[1:]):
        block = bytearray()
        while start < end:
            token = payload[start]
            start += 1
            if token == ESCAPE:
                block.append(payload[start])
                start += 1
            else:
                block.extend(phrases[token])
        if len(block) != min(BLOCK, POOL_END - POOL - len(raw)):
            raise ValueError("Wrong OEM dictionary block length")
        raw.extend(block)
    return bytes(raw)

@lru_cache(maxsize=2)
def pack_crt(raw):
    """Literal runs carry 1..128 bytes. Backreferences copy 3..18 bytes up to 2048 back."""
    if len(raw) != CRT_SIZE:
        raise ValueError("Invalid SRAM initializer size")
    cost = [0]*(len(raw)+1)
    choices = [None]*len(raw)
    for p in range(len(raw)-1, -1, -1):
        best, count = min((1+n+cost[p+n], n) for n in range(1, min(128, len(raw)-p)+1))
        choice = count, 0
        for n in range(3, min(18, len(raw)-p)+1):
            match = raw.rfind(raw[p:p+n], max(0, p-2048), p+n-1)
            if match < 0:
                break
            if 2+cost[p+n] <= best:
                best, choice = 2+cost[p+n], (n, p-match)
        cost[p], choices[p] = best, choice
    out = bytearray()
    p = 0
    while p < len(raw):
        n, back = choices[p]
        if back:
            out.extend(struct.pack('>H', 0x8000 | (n-3)<<11 | (back-1)))
        else:
            out.append(n-1)
            out.extend(raw[p:p+n])
        p += n
    if len(out) > HIGH_START - CRT or unpack_crt(out) != raw:
        raise ValueError("Cannot fit the original SRAM initializer")
    return bytes(out)

def unpack_crt(packed):
    pos = 0
    out = bytearray()
    while pos < len(packed) and len(out) < CRT_SIZE:
        tag = packed[pos]
        pos += 1
        if tag & 128:
            if pos >= len(packed):
                raise ValueError("Truncated SRAM initializer")
            distance = ((tag & 7)*256 + packed[pos]) + 1
            pos += 1
            if distance > len(out):
                raise ValueError("Invalid initializer distance")
            for _ in range(((tag >> 3) & 15)+3):
                out.append(out[-distance])
        else:
            count = tag+1
            if pos+count > len(packed):
                raise ValueError("Truncated SRAM initializer")
            out.extend(packed[pos:pos+count])
            pos += count
    if len(out) != CRT_SIZE or any(b != 255 for b in packed[pos:]):
        raise ValueError("Invalid initializer payload")
    return bytes(out)

# The only stock indexed lookup at 0x2177 belongs to the 1-Wire CRC-8
# implementation at 0x194c4. The mathematical table reconstructs all 256 bytes.
CRC_TABLE = 0x2177

def sensor_crc_table():
    table = bytearray()
    for value in range(256):
        for _ in range(8):
            value = (value >> 1) ^ (0x8c if value & 1 else 0)
        table.append(value)
    return bytes(table)

# Byte-for-byte matches to the Arduino AVR GCC 7.3.0 library routines.
# All branch targets in these routines stay within their own functions,
# except the GCC prologue's tail IJMP to the caller-supplied continuation.
LIBRARY = (
    ("strcpy", 0x26c0, 14, "f30c4ee614dce0e9d19c1a056cbf61598dc3c7a1432db186dbc5d16a3b049524"),
    ("__udivmodhi4", 0x1b0ee, 40, "90387627bc91fd8f3cf847061caa5b178777e10e15dab7cf7acce6325edef02d"),
    ("__udivmodsi4", 0x1b13c, 68, "c6dad14f0a7c526cd400f555fca6be297b9299ad5b0c7c157765caaebb265416"),
    ("__epilogue_restores__", 0x1b210, 54, "e2079218e9976c1bb5465c14765b5c73677c37c86594bd49278c90b9da5180ca"),
    ("__prologue_saves__", 0x1b1d8, 56, "d125a300e583db2016168f7e49e082e3a7227fa9ea384a5310f60b94520ef3d4"),
)

def verify_library(raw):
    for name, pc, count, expected in LIBRARY:
        if hashlib.sha256(raw[pc:pc+count]).hexdigest() != expected:
            raise ValueError("Unrecognized original AVR routine: " + name)
