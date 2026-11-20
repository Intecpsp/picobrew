#!/usr/bin/env python3
"""Select a product model and dispatch to its implemented firmware component."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "build": "standalone", "test": "standalone-test", "bootstrap": "bootstrap",
    "usb-test": "usb-test", "browser-test": "web-test", "package": "firmware-package",
}


def load_models():
    document = json.loads((ROOT / "firmware/models.json").read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("Unsupported firmware model registry version")
    result = {}
    for item in document["models"]:
        if (not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", item["id"])
                or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", item["family"])
                or item["id"] in result or not isinstance(item["custom_firmware"], bool)):
            raise ValueError("Invalid or duplicate firmware model registry entry")
        result[item["id"]] = item
    return result


def resolve_model(model="", machine=""):
    models = load_models()
    model, machine = model.strip(), machine.strip()
    if not model:
        if machine in models:
            model = machine
        elif machine in ("", "pico-s-pro"):
            model = "pico-pro"
        else:
            members = [entry["id"] for entry in models.values() if entry["family"] == machine]
            if len(members) != 1:
                raise ValueError(f"Unknown or ambiguous firmware family: {machine}")
            model = members[0]
    if model not in models:
        raise ValueError(f"Unknown firmware model {model!r}; choose {', '.join(models)}")
    selected = models[model]
    if machine and machine not in (model, selected["family"]):
        raise ValueError(f"Model {model} belongs to {selected['family']}, not {machine}")
    if not selected["custom_firmware"]:
        raise ValueError(
            f"{selected['name']} has no implemented custom firmware yet. "
            "The model identifier is reserved; another machine's image cannot be relabeled for it."
        )
    return selected


def make_command(action, selected, make="make", python=sys.executable, display=""):
    variants = selected.get("display_variants", [])
    if display and display not in variants:
        raise ValueError("The selected model does not support that display variant")
    if variants and action in ("build", "package") and display not in variants:
        raise ValueError("Pico C requires DISPLAY_VARIANT=ssd1325 or DISPLAY_VARIANT=ssd1309 after verifying the physical controller")
    if selected['id'] == 'pico-c' and action == 'usb-test':
        raise ValueError("Pico C has no qualified USB installation/recovery tool yet")
    command = [make, "-C", str(ROOT / "firmware" / selected["family"] / "modified"),
               TARGETS[action], f"MODEL={selected['id']}", f"PYTHON={python}"]
    if display:
        command.append(f"DISPLAY_VARIANT={display}")
    return command


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=[*TARGETS, "list"])
    parser.add_argument("--model", default="", help="Product model; defaults to pico-pro")
    parser.add_argument("--machine", default="", help="Product model or source family directory")
    parser.add_argument("--display", default="", help="Verified controller variant; required for C images")
    parser.add_argument("--make", default="make", help="GNU Make executable")
    parser.add_argument("--python", default=sys.executable, help="Python executable for component tools")
    args = parser.parse_args(argv)
    try:
        if args.action == "list":
            for entry in load_models().values():
                state = "implemented" if entry["custom_firmware"] else "planned; no custom image"
                print(f"{entry['id']}: {entry['name']} ({entry['family']}; {state})")
            return 0
        selected = resolve_model(args.model, args.machine)
        return subprocess.run(make_command(args.action, selected, args.make, args.python, args.display),
                              cwd=ROOT, check=False).returncode
    except (ValueError, OSError) as error:
        print(f"Firmware build: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
