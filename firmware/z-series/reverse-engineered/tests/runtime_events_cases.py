"""Payload values that independently distinguish all original event fields."""
import itertools


def cases():
    result = []
    for event, previous, current, notification, pending in itertools.product(
            (0, 1, 2, 0xffffffff), (0, 2, 3), (0, 2, 3),
            (0, 1, 0x12345678, 0xffffffff), (0, 1)):
        result.append(dict(words=[event, previous, current, notification, pending]))
    return result
