"""Independent inputs for Z119 Buzzer, alarm and console instruction replay."""
import itertools
import random


def state(**values):
    keys = ['channel', 'resolution', 'pin', 'frequency', 'duty', 'enabled',
            'previous', 'phase', 'alarm', 'changed', 'now', 'step']
    result = [1, 8, 13, 4800, 125, 0, 0, 0, 0, 123, 1000, 17]
    for key, value in values.items():
        result[keys.index(key)] = value
    return result


def cases():
    result = []
    def add(words, actions, text=''):
        result.append(dict(state=words, actions=actions, text=text))
    add(state(frequency=999, duty=999, enabled=1, previous=1, phase=1), [[10, 0]])
    for enabled, previous, phase in itertools.product(range(2), repeat=3):
        for frequency, duty in [(0, 0), (1, 1), (4800, 125), (0xffffffff, 0xffffffff)]:
            words = state(enabled=enabled, previous=previous, phase=phase, frequency=frequency, duty=duty)
            for actions in [[[0, 0]], [[0, 1]], [[1, 0]], [[1, 0]] * 5,
                            [[0, 1], [1, 0], [1, 0], [0, 0], [1, 0], [1, 0]],
                            [[3, 0]], [[4, 0]], [[5, 0]], [[6, 0]], [[7, 0]]]:
                add(words, actions)
    for enabled, alarm, requested in itertools.product(range(2), repeat=3):
        for now, step in [(0, 1), (1000, 17), (0xfffffff0, 17), (0xffffffff, 0)]:
            add(state(enabled=enabled, alarm=alarm, now=now, step=step), [[2, requested]])
    for enabled, alarm in [(0, 1), (1, 0)]:
        for now in (1000, 0xfffffff0):
            add(state(enabled=enabled, alarm=alarm, now=now),
                [[2, alarm], [2, enabled], [1, 0], [2, 1-enabled], [1, 0]])
    for text in ['', '  4800', '+125', '-1', '0', '2147483647', '-2147483648',
                 '100x', 'x100', '\t\n +00123 remainder', '+', ' -0']:
        for operation in (8, 9):
            add(state(), [[operation, 0], [6, 0], [1, 0], [1, 0], [7, 0], [1, 0]], text)
    rng = random.Random(0x40080570)
    for _ in range(180):
        enabled = rng.randrange(2)
        words = state(channel=rng.randrange(256), resolution=rng.randrange(256), pin=rng.randrange(256),
                      frequency=rng.randrange(2**32), duty=rng.randrange(2**32), enabled=enabled,
                      previous=rng.randrange(2), phase=rng.randrange(2), alarm=enabled,
                      changed=rng.randrange(2**32), now=rng.randrange(2**32), step=rng.randrange(100))
        choice = rng.randrange(5)
        if choice == 0:
            actions = [[1, 0]] * rng.randrange(1, 12)
        elif choice == 1:
            actions = [[3, 0], [5, 0]]
        elif choice == 2:
            actions = [[2, 1], [1, 0], [1, 0], [2, 0], [1, 0], [1, 0]]
        elif choice == 3:
            actions = [[8, 0], [6, 0], [1, 0], [7, 0], [1, 0]]
        else:
            actions = [[9, 0], [5, 0], [6, 0], [1, 0]]
        add(words, actions, str(rng.randrange(-(2**31), 2**31)))
    return result
