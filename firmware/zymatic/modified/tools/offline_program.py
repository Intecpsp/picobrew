"""Validate and serialize one stock-routed Zymatic program for private USB provisioning."""

import hashlib
import json
from pathlib import Path
import re
import struct

import usb_stock

SIZE = 516
MAX_RECORD = 511
STEP_FIELDS = ("name", "target_f", "minutes", "location", "drain_minutes")
# Source file and ingredient guidance are recorded in ../offline-recipe.md.
DEFAULT = {
    "name": "American Brown Ale",
    "id": "cc7f7047d96443d04dfcda7e08d50b69",
    "steps": [dict(zip(STEP_FIELDS, step)) for step in (
        ("Heat Water", 152, 0, 0, 0), ("Single Infusion", 152, 90, 1, 8),
        ("Heat to Boil", 207, 0, 0, 0), ("Hops 1", 207, 45, 2, 0),
        ("Hops 2", 207, 5, 3, 0), ("Hops 3", 207, 8, 4, 0),
        ("Hops 4", 207, 2, 5, 10), ("Connect Chiller", 0, 0, 6, 0),
        ("Chill", 65, 10, 0, 10),
    )],
}


def label(value):
    if (not isinstance(value, str) or not 1 <= len(value) <= 20 or not value.strip()
            or any(not 32 <= ord(c) <= 126 or c in "#|/," for c in value)):
        raise ValueError("Use a 1–20 character printable ASCII label without stock protocol delimiters")
    return value


def pack_program(program=None):
    """Pack an already model-selected MachineRecipe and reject unrepresentable fields."""
    if program is None:
        program = DEFAULT
    if (not isinstance(program, dict) or set(program) - {"id", "name", "steps", "model"}
            or program.get("model", "zymatic") != "zymatic"):
        raise ValueError("Expected a Zymatic native program")
    name = label(program.get("name"))
    steps = program.get("steps")
    if not isinstance(steps, list) or not 1 <= len(steps) <= 25:
        raise ValueError("A Zymatic program requires 1–25 steps")
    records = []
    for i, step in enumerate(steps):
        if not isinstance(step, dict) or set(step) != set(STEP_FIELDS):
            raise ValueError(f"Step {i + 1} requires a name, target_f, minutes, location and drain_minutes")
        fields = [label(step["name"])]
        for key, maximum in zip(STEP_FIELDS[1:], (207, 255, 6, 255)):
            value = step[key]
            if type(value) is not int or not 0 <= value <= maximum:
                raise ValueError(f"Step {i + 1} {key} must be an integer between 0 and {maximum}")
            fields.append(str(value))
        if step["location"] == 6 and i + 1 == len(steps):
            raise ValueError("A Zymatic pause needs a following step for the physical confirmation screen")
        records.append(",".join(fields) + "/")
    schedule = "".join(records)
    ident = program.get("id")
    if ident is None:
        ident = hashlib.sha256((name + "/" + schedule).encode("ascii")).hexdigest()[:32]
    if not isinstance(ident, str) or not re.fullmatch("[0-9a-f]{32}", ident):
        raise ValueError("Use a 32-character lowercase hexadecimal program ID or omit the ID")
    wire = (name + "/" + ident + "/" + schedule).encode("ascii")
    if len(wire) > MAX_RECORD:
        raise ValueError("This program exceeds the stock 511-byte encoded recipe limit")
    return struct.pack("<BHB", len(steps), len(wire), len(name)) + wire.ljust(512, b"\0")


def unpack_program(block):
    """Decode a record and check its field bounds, canonical text and zero padding."""
    if not isinstance(block, bytes) or len(block) != SIZE:
        raise ValueError("Incorrect offline program field size")
    nsteps, length, namelen = struct.unpack_from("<BHB", block)
    if not 1 <= length <= MAX_RECORD or block[4+length:].strip(b"\0"):
        raise ValueError("Invalid offline program length or padding")
    try:
        name, ident, *lines, trailing = block[4:4+length].decode("ascii").split("/")
    except (ValueError, UnicodeError) as err:
        raise ValueError("Invalid offline recipe record") from err
    if trailing or len(name) != namelen or len(lines) != nsteps:
        raise ValueError("Offline program header disagrees with its wire record")
    steps = []
    for line in lines:
        fields = line.split(",")
        if len(fields) != 5 or any(not re.fullmatch(r"0|[1-9][0-9]{0,2}", f) for f in fields[1:]):
            raise ValueError("Invalid offline program step")
        steps.append(dict(zip(STEP_FIELDS, [fields[0], *map(int, fields[1:])])))
    result = {"name": name, "id": ident, "steps": steps}
    if pack_program(result) != block:
        raise ValueError("Offline program is not canonical")
    return result


def read_program(path):
    """Read a USB JSON export with an explicit model to disambiguate native route numbers."""
    if path is None:
        return None
    raw = usb_stock.private_files.read_regular(Path(path), 65536)
    value = json.loads(raw)
    if not isinstance(value, dict) or value.get("model") != "zymatic":
        raise ValueError('An offline program JSON file must specify "model": "zymatic"')
    return unpack_program(pack_program(value))
