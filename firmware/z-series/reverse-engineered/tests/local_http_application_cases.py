"""POST integration cases; actual JSON parser, writer and mailbox execute."""
import json
from pathlib import Path
import sys
from program_recipe_cases import local, step

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from validate_core_event import seed


def cases():
    inputs = [("empty-recipe", local(), 100, 101325),
              ("mash", local([step()]), 100, 101325),
              ("pause", local([step(StepType=2)]), 100, 101325),
              ("boil-compensation", local([step(Temp=101, Time=900)]), 96, 90000)]
    for beer in (False, True):
        for pak in (False, True):
            inputs.append(("flags-%s-%s" % (beer, pak),
                           local([step()], IsBeer=beer, UsePicoPakMode=pak), 100, 101325))
    for location in (0, 1, 2, 6, 7):
        inputs.append(("location-%u" % location, local([step(Location=location, Temp=97)]), 100, 101325))
    for temperature in (0, 30, 94.45, 94.5, 99):
        inputs.append(("temperature-%s" % temperature, local([step(Temp=temperature)]), 100, 101325))
    for count in (2, 5):
        inputs.append(("steps-%u" % count, local([step(Name="Step %u" % n) for n in range(count)]), 100, 101325))
    for name, text in [("empty", ""), ("whitespace", " \t\n"), ("incomplete", '{"Name":'),
                       ("invalid", '{"Name":@}'), ("deep", '[' * 11 + '0' + ']' * 11),
                       ("pool", json.dumps([0] * 65)), ("null", "null"), ("array", "[]"),
                       ("plain-object", "{}"), ("unicode", local(Name="Crème"))]:
        inputs.append((name, text, 100, 101325))
    result = []
    for pending in (False, True):
        for name, text, maximum, pressure in inputs:
            result.append(dict(id="%s-pending-%s" % (name, pending), path="/recipe", json=text,
                               maximum=maximum, pressure=pressure, pending=pending,
                               current_hex=seed(1), queued_hex=seed(3)))
        for path in ("", "/Recipe", "/recipe?x=1", "/other"):
            result.append(dict(id="path-%s-pending-%s" % (path, pending), path=path, json=local([step()]),
                               maximum=100, pressure=101325, pending=pending,
                               current_hex=seed(1), queued_hex=seed(3)))
    return result
