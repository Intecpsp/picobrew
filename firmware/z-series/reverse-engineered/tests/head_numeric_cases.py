"""Original Head numeric input domains, edit modes, flags and bit payloads."""
import struct


def number(value, unit):
    return list(struct.unpack("<II", struct.pack("<d", value))) + [unit]


def parameters(program, seed):
    return [program, seed, seed + 1] + number(seed + 2, 6) + number(seed + 3, 0) + number(seed + 4, 7) + number(seed + 5, 4) + [1]


def case(operation, value, *, manual=0, flags=0, program=20, abv=0, ibu=0):
    state = [manual, 2] + number(160, 5) + number(3600, 6) + number(300, 6)
    state += [(flags >> i) & 1 for i in range(4)]
    state += parameters(program, 10) + parameters(23, 30) + [abv, ibu]
    return dict(operation=operation, state=state, value=list(value))


def cases():
    result = []
    for bits in [0, 0x80000000, 0x3f800000, 0xc1200000, 0x42be0000, 0x7f800000, 0xff800000, 0x7fc12345, 0x7f7fffff]:
        result.append(case(0, [bits, 0, 0, 0]))
    for value in [0, 1, 59, 60, 3600, 86400, 0x7fffffff, 0xffffffff]:
        result.append(case(1, [value, 0, 0, 0]))
    for manual in [0, 1]:
        for flags in range(16):
            for location in list(range(8)) + [0xffffffff]:
                result.append(case(2, [location, 0, 0, 0], manual=manual, flags=flags))
    for manual in [0, 1]:
        for program in list(range(29)) + [0xffffffff]:
            for quantity in list(range(9)) + [0xffffffff]:
                result.append(case(3, [quantity] + number(123.25, 4), manual=manual, program=program))
    for quantity in [0, 1, 3, 4, 5, 6, 7]:
        for flags in range(16):
            for lo, hi, unit in [number(0, 8), number(-12.5, 5), number(1e300, 0), [0, 0x80000000, 0xffffffff],
                                 [0x12345, 0x7ff80000, 4], [0, 0x7ff00000, 6]]:
                result.append(case(3, [quantity, lo, hi, unit], manual=int(quantity >= 5), flags=flags, program=23))
    for abv in [0, 1]:
        for ibu in [0, 1]:
            for value in list(range(12)) + [0x80000000, 0x7fffffff, 0xffffffff]:
                result.append(case(4, [value, 0, 0, 0], abv=abv, ibu=ibu))
    return result
