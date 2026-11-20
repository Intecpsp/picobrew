"""Deterministic original JSON and recipe-builder application inputs."""
import json


def case(text, action=0, maximum=100, pressure=101325, capacity=1024, nesting=10,
         abv=-1, ibu=-1, resumed=0, session=None):
    return dict(action=action, json=text, maximum=maximum, pressure=pressure,
                capacity=capacity, nesting=nesting, abv=abv, ibu=ibu, resumed=resumed,
                session=(7 if action == 3 else 8) if session is None else session)


def local(steps=(), **fields):
    value = dict(Name="Fixture recipe", IsBeer=True, UsePicoPakMode=False,
                 Steps=list(steps))
    value.update(fields)
    return json.dumps(value, separators=(",", ":"))


def step(**fields):
    value = dict(Name="Mash", StepType=0, Temp=67.5, Location=2,
                 Time=600, Drain=30)
    value.update(fields)
    return value


def http(steps=(), **fields):
    value = dict(Name="HTTP recipe", TypeCode="Beer", Abv=5, Ibu=4, Steps=list(steps))
    value.update(fields)
    return json.dumps(value, separators=(",", ":"))


def http_step(**fields):
    value = dict(Name="Mash", Action=0, Temp=153, Location=2, Time=30, Drain=1,
                 MinTime=10, MaxTime=60)
    value.update(fields)
    return value


def cases():
    result = [case(local(), 1), case(local([step()]), 1),
              case(local([step(Temp=99.0, Time=601)]), 1),
              case(local([step(StepType=2)]), 1)]
    for text in ('', '{}', '[]', 'null', 'true', 'false', '123', '-2',
                 '67.5', '{"a":true,"b":[1,2,"text"]}', '{"a":',
                 '{"a":"\\u00e9"}', '{"a":1,}', '{"a":1} trailing',
                 '{unquoted:3}', "{'single':'quoted'}"):
        result.append(case(text))
    for text in (' ', '\n\t', '[', '"', '1x', 'truex', 'trux', 'falsx',
                 'nulx', 'True', 'NaN', 'Infinity', '1e400', '1e-400',
                 '-2147483648', '4294967295', '4294967296',
                 '"abc\\nxyz"', '"bad\\q"', '"\\u0000"', '"\\uZZZZ"',
                 '{"Name":"Repeated","Name":"Last","Steps":[]}',
                 '[0,1,2] garbage', '1 2', '{"a":1/*x*/}', '//x\n{}'):
        result.append(case(text))
    for depth in range(8, 13):
        result.append(case('[' * depth + '0' + ']' * depth))
    for capacity in (0, 16, 32, 64, 1024, 4096):
        for count in sorted({0, 1, max(0, capacity // 16 - 1),
                             capacity // 16, capacity // 16 + 1}):
            result.append(case(json.dumps([0] * count), capacity=capacity))
    for count in (60, 63, 64, 65):
        result.append(case(json.dumps({str(i): i for i in range(count)})))
    for location in (-1, 0, 1, 2, 6, 7):
        for temperature in (0, 67.5, 94.45, 94.5, 99):
            for seconds in (600, 601):
                result.append(case(local([step(Location=location, Temp=temperature,
                                               Time=seconds)]), 1))
    for maximum in (0, 32, 34, 80, 96, 100):
        for temperature in (0, 30, 97, 101):
            result.append(case(local([step(Temp=temperature, Time=900)]), 1,
                               maximum=maximum))
    for flags in ((False, False), (False, True), (True, False), (True, True)):
        result.append(case(local([step()], IsBeer=flags[0],
                                 UsePicoPakMode=flags[1]), 1))
    for count in range(1, 11):
        result.append(case(local([step(Name="Step %d" % i) for i in range(count)]), 1))
    for value in (None, False, True, -1, 0, 1, "", "false", "0", {}, []):
        result.append(case(local([step()], IsBeer=value, UsePicoPakMode=value), 1))
    for field in ("StepType", "Temp", "Location", "Time", "Drain"):
        for value in (None, False, True, -1, "-1", "67.5", "600", {}, []):
            result.append(case(local([step(**{field: value})]), 1))
    for text in ('/* "\\u0000" */ {}', '// "\\u0000"\n{}',
                 '"\\q\\v\\x"', '"\\\\u0000"', '"\\\\\\u0000"',
                 '{"a":1,"b":"\\u0000"}', '[invalid,"\\u0000"]',
                 local().replace('Fixture recipe', 'Raw\\qname')):
        result.append(case(text, int('Fixture' not in text and 'Steps' in text)))
    for action in (2, 3):
        result.append(case(http(), action))
        result.append(case(http([http_step()]), action))
        for wanted in (0, 3, 7, 10, -1, 11):
            result.append(case(http([http_step()]), action, abv=wanted, ibu=wanted))
        for location in (-1, 0, 1, 2, 5, 6, 7):
            for temperature in (0, 32, 153, 202, 203, 210, 212):
                for minutes in (10, 11):
                    result.append(case(http([http_step(Location=location, Temp=temperature,
                        Time=minutes)]), action))
        for value in (None, False, True, "12", 0, 0.1, 6.81, 11.36, 24):
            result.append(case(http([http_step()], StartWater=value), action))
        for temperature in (180, 212):
            result.append(case(http([http_step(Temp=temperature)]), action, resumed=1))
        for maximum in (32, 34, 80, 96):
            result.append(case(http([http_step(Temp=212)]), action, maximum=maximum))
    for location in (2, 3, 4, 5, 6):
        for baseline in (1, 5, 9):
            for wanted in (0, 3, 7, 10):
                result.append(case(http([http_step(Location=location)], Abv=baseline,
                                       Ibu=baseline), 3, abv=wanted, ibu=wanted))
    for value in (-1, 0, 1, 2, 4):
        result.append(case(http([http_step(Action=value)]), 3))
    for minimum, maximum in ((None, None), ("10", "60"), (10.0, 60.0),
                             (-1, 60), (10, -1), (30, 30), (60, 10), (0, 0)):
        result.append(case(http([http_step(MinTime=minimum, MaxTime=maximum)]), 3, abv=7))
    for type_code in (None, "", "Beer", "beer", "Coffee", 1):
        result.append(case(http([http_step()], TypeCode=type_code), 2, session=6))
    for count in (63, 64, 65, 124, 125, 126):
        result.append(case(http([dict(Name="s%d" % index) for index in range(count)]), 2))
    for action in (1, 2, 3):
        for name in ("", "x" * 47, "x" * 48, "x" * 80):
            result.append(case(local([step()], Name=name) if action == 1 else
                               http([http_step()], Name=name), action))
        for name in ("", "x" * 47, "x" * 48, "x" * 80):
            result.append(case(local([step(Name=name)]) if action == 1 else
                               http([http_step(Name=name)]), action))
    return result
