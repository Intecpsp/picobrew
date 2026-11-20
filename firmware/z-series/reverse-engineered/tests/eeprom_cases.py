"""Deterministic original EEPROM data/lifecycle input states, not expected outputs."""
import itertools
import random
import struct


def data(**values):
    value = bytearray(120)
    for name, offset in {"version": 0, "language": 8, "session": 108, "azure": 112, "boiler": 116}.items():
        struct.pack_into("<I", value, offset, values.get(name, 0))
    value[4:8] = bytes(values.get("flags", [0, 0, 0, 0]))
    if "ssid" in values:
        value[12:44] = values["ssid"]
    if "password" in values:
        value[44:108] = values["password"]
    return bytes(value)


def cases():
    result = []
    def add(group, action, value=None, **options):
        result.append(dict(group=group, action=action, data=(data() if value is None else value).hex(),
            cache=options.pop("cache", bytes([0xa5] * 120)).hex(), initialized=options.pop("initialized", 0),
            dirty=options.pop("dirty", 0), requested=options.pop("requested", 512),
            actual=options.pop("actual", 512), begin=options.pop("begin", 1),
            commit=options.pop("commit", 1), text=options.pop("text", None), **options))
    rng = random.Random(0x5a119e)
    for values in itertools.product((0, 1, 2, 3, 0xffffffff), repeat=4):
        value = data(language=values[0], session=values[1], azure=values[2], boiler=values[3],
                     flags=[0, 1, 127, 255])
        add("enums", 1, value)
        add("validity", 2, value)
    for length in range(33):
        ssid = bytearray(b"s" * 32)
        if length < 32: ssid[length] = 0
        add("ssid_terminators", 2, data(ssid=ssid))
    for length in range(65):
        password = bytearray(b"p" * 64)
        if length < 64: password[length] = 0
        add("password_terminators", 2, data(password=password))
    for version in (1, 2, 0x7fffffff, 0x80000000, 0xffffffff):
        add("version", 2, data(version=version))
    for _ in range(64):
        value = bytes(rng.getrandbits(8) for _ in range(120))
        for action in (0, 1, 2): add("raw_bytes", action, value)
    texts = [None, "", "0", "off", "false", "OFF", "False", "true", "1", " 0", "0 ", "false\n", "on"]
    texts += ["x" * length for length in (1, 30, 31, 32, 33, 62, 63, 64, 65, 256)]
    initial = data(flags=[1, 2, 3, 4], ssid=b"S" * 32, password=b"P" * 64)
    for text in texts:
        for action in (3, 4, 5, 10, 11, 12, 13):
            add("text", action, initial, text=text)
    states = [data(), data(boiler=1, flags=[255, 128, 2, 1]), data(language=3), data(version=1, boiler=1),
              data(ssid=b"x" * 32), data(password=b"x" * 64), bytes([255] * 120),
              data(language=0xffffffff, session=3, azure=3, boiler=1, version=2)]
    for stored, actual, begin, commit in itertools.product(states, (0, 119, 120, 512), (0, 1), (0, 1)):
        add("setup", 6, data(boiler=1, flags=[3, 4, 5, 6]), cache=stored, actual=actual, begin=begin, commit=commit)
    for requested in (0, 1, 119, 120, 512, 0xffffffff):
        for begin in (0, 1): add("setup_size", 6, requested=requested, begin=begin)
    for action, value, initialized, actual, commit, dirty in itertools.product(
            (7, 8), states, (0, 1), (0, 119, 120, 512), (0, 1), (0, 1)):
        add("write_reset", action, value, initialized=initialized, actual=actual, commit=commit, dirty=dirty)
    for initialized in (0, 1): add("getter", 9, initialized=initialized)
    for action in (14, 15, 16): add("boiler_commands", action, bytes([0xaa] * 120))
    return result
