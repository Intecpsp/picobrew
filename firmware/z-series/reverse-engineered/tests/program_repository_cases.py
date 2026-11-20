"""Bounded original ProgramBuffer/Writer/Resolver inputs; no derived expected values."""
import itertools


def parameters(**changes):
    result = dict(id=20, abv=-1, ibu=-1, time=[1800, 6], water=[11.36, 0],
                  intensity=[7, 7], temperature=[200, 5], resumed=0)
    result.update(changes)
    return result


def cases():
    result = []
    def add(group, action, **changes):
        value = dict(group=group, action=action, initial_code=2, initial_strings=3,
                     args=[], text="Fixture program", code=[], null_code=0,
                     parameters=parameters(), pressure=98765.25, maximum=96.5, added=123, imperial=0)
        value.update(changes)
        result.append(value)
    for identifier in list(range(31)) + [0x7fffffff, 0x80000000, 0xffffffff]:
        add("resolver", 0, args=[identifier])
        add("buffered_predicate", 15, args=[identifier])
        add("static_stats", 13, args=[identifier])
    for strings, code in itertools.product((0, 1, 3, 64), (0, 1, 10, 255, 256)):
        add("reset", 1, initial_code=code, initial_strings=strings)
    for text in [None, "", "name", "n" * 46, "n" * 47, "n" * 48, "n" * 255]:
        for kind in (0, 10, 16, 0xffffffff): add("start", 2, text=text, args=[kind, 123])
        for count in (0, 1, 63, 64): add("append_string", 4, text=text, initial_strings=count)
        add("begin_coffee", 8, text=text)
    for count, opcode, argument in itertools.product((0, 1, 254, 255, 256), (0, 2, 9, 57, 58, 0xffffffff), (0, 0xffffffff)):
        add("append_code", 3, initial_code=count, args=[opcode, argument])
    for count, null in itertools.product((0, 1, 2, 255, 256, 257), (0, 1)):
        add("set_code", 5, code=[[9, i] for i in range(count)], null_code=null)
    for liters in (-1, 0, 0.099999, 0.1, 1, 3.78541, 11.36, 25, 1000):
        add("heating_rate", 6, args=[liters])
    for value, unit in itertools.product((-1.9, 0, 0.9, 1, 7.9, 14.9, 15, 15.9, 100), (0, 6, 7, 8)):
        add("intensity", 7, parameters=parameters(intensity=[value, unit]))
    for count in (0, 240, 250, 255, 256):
        add("cold_steps", 9, initial_code=count)
    for fahrenheit, minutes in itertools.product((49, 50, 51, 160, 200, 205, 206), (-1, 0, 1, 15, 0x7fffffff)):
        add("hot_steps", 10, args=[fahrenheit, minutes])
    for count in (250, 255, 256): add("hot_capacity", 10, initial_code=count, args=[200, 7])
    for water, unit in itertools.product((-1, 0, 0.0999, 0.1, 1, 11.36, 15), (0, 1, 2, 8)):
        add("finish", 11, parameters=parameters(water=[water, unit]))
    for identifier, water, unit, intensity, imperial in itertools.product(
            (20, 21, 22, 23), (0.1, 3.78541, 11.36), (0, 1), (0, 7.9, 30), (0, 1)):
        add("coffee", 12, parameters=parameters(id=identifier, water=[water, unit], intensity=[intensity, 7]), imperial=imperial)
    for fahrenheit, seconds in itertools.product((49, 50, 50.9, 200.9, 205, 205.9, 206), (-60, 0, 59, 60, 1800)):
        add("custom_coffee", 12, parameters=parameters(id=23, temperature=[fahrenheit, 5], time=[seconds, 6]))
    for identifier in (19, 20, 21, 22, 23, 24, 0xffffffff):
        for unit in (2, 7, 8): add("coffee_invalid", 12, parameters=parameters(id=identifier, water=[1, unit]))
    for identifier in (21, 22):
        add("coffee_intensity_unit", 12, parameters=parameters(id=identifier, intensity=[5, 6]))
    for identifier in (20, 21, 22, 23):
        add("coffee_small_volume", 12, parameters=parameters(id=identifier, water=[0.01, 0]))
    for identifier in (1, 2, 12, 19, 25, 28):
        for pressure, boiling in ((101325, 100), (85000, 94), (0, 0)):
            add("static_environment", 13, args=[identifier], pressure=pressure, maximum=boiling)
    for code in ([], [[2, 60*256], [3, 60], [5, 120], [2, 70*256]],
                 [[57, 0], [6, 2], [7, 0x00001000], [18, 0], [56, 0]],
                 [[26, 77], [20, 0], [24, 0]], [[2, 0]], [[9, 0]]):
        for wort, rate in itertools.product((0, 12.77, 80), (0, 0.01, 0.011, 1.5)):
            add("duration", 14, code=code, args=[wort, rate])
    return result
