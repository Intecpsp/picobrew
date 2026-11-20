"""Exhaustive valid Z119 text lookup inputs and reviewed invalid boundaries."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import generate_ui_assets as catalog


def cases():
    result = []
    for category, (_, _, function, _, count) in enumerate(catalog.TEXT):
        for item in list(range(count)) + [count, count + 1, 0x7fffffff, 0xffffffff]:
            for language in [0, 1, 2, 3, 0xffffffff]:
                result.append(dict(kind="label", category=category, function=function, item=item, language=language))
    for screen in list(range(104)) + [104, 0x7fffffff, 0xffffffff]:
        for language in range(3):
            result.append(dict(kind="screen", category=0, function=0x400d9a2c, item=screen, language=language))
    return result
