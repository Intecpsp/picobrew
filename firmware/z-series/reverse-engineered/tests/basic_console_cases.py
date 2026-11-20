"""Fixed diagnostic vectors, including returning assertions and timer wrapping."""

LEAVES = [
    '4010e2b4', '400e18a4', '4010e580', '400e1e90', '401015c0',
    '400fa974', '400e2c20', '4010f1c4', '40109dbc', '4010cb60',
    '400e2f4c', '400f195c', '400e3004', '400e32c4', '40117578',
    '4011272c', '400d79ac', '400e38dc', '400e3a54', '400e3f10',
    '400ed224', '400ecad0', '400e4544', '40101844', '400eb870',
    '400e5158', '4010e0e4', '400ed2ec', '400e1048', '400e55a0',
    '400f560c', '400db514', '400db56c', '40111df8', '4010f418',
    '400e56d4', '401011a4',
]


def cases():
    result = []
    for callback in LEAVES:
        for clock, water, counter in [
                (1000, 4096, 7), (0, 701, 0),
                (0xffffff80, 700, 0xffffffff), (0x80000001, 0, 0xfffffffe)]:
            case = dict(callback=callback, clock=clock, counter=counter, water=water)
            if callback in ('400ed2ec', '400e1048'):
                case['argument'] = 0x3f4289d8  # Original built-in CLEAN Program.
            result.append(case)
    result.extend([
        dict(callback='4011ed44', clock=1000, counter=7, water=4096),
        dict(callback='4011ed44', clock=0xffffff80, counter=0xffffffff, water=700),
        dict(callback='4011fbac', clock=0, counter=0, water=701),
        dict(callback='4011fbac', clock=0x80000001, counter=0xfffffffe, water=0),
    ])
    return result
