"""Core orchestration boundaries and complete BinaryDownload<16> diagnostic."""
def cases():
    result = []
    profiles = [(1000, 7, 0, 0), (0xfffffff0, 0xffffffff, 17, 1),
                (23, 3, 1234, 31), (0x80000001, 0xfffffffe, 1, 4)]
    for clock, counter, delay, assertions in profiles:
        common = dict(clock=clock, counter=counter, water=4096, network=7,
                      leaf_delay=delay, leaf_assertions=assertions)
        for available, kind in [(0, 0), (1, 2), (1, 0)]:
            result.append(dict(common, operation=0, ethernet=0, wifi=0, available=available, client_class=kind))
        for operation in [1, 2]:
            for ethernet, wifi, available, kind in [(0, 0, 0, 0), (0x0100007f, 0x0200007f, 1, 1),
                    (0, 0x0100007f, 1, 2), (1, 0, 0, 0), (0, 1, 1, 0)]:
                result.append(dict(common, operation=operation, ethernet=ethernet, wifi=wifi, available=available, client_class=kind))
        for operation in [3, 4, 5]:
            result.append(dict(common, operation=operation, ethernet=1, wifi=2, available=1, client_class=1))
    return result
