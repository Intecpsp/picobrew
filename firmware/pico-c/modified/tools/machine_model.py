"""Pico C build identity and explicit OLED compatibility selection."""
import hashlib
import json
from pathlib import Path

MODELS = {"pico-c": "PICO_MODEL_PICO_C"}
DEFAULT_MODEL = "pico-c"
DISPLAYS = ("ssd1325", "ssd1309")


def add_model_argument(parser):
    parser.add_argument("--model", choices=tuple(MODELS), default=DEFAULT_MODEL)
    parser.add_argument("--display", choices=DISPLAYS, required=True,
                        help="Verified OLED controller variant; never inferred from the model")


def model_flag(model):
    return "-DPICO_MODEL=" + MODELS[model]


def display_flag(display):
    if display not in DISPLAYS:
        raise ValueError("Unknown Pico C display variant")
    return "-DPICO_C_DISPLAY_SSD1309=" + str(int(display == "ssd1309"))


def verify_archive_model(directory, model, standalone, display):
    directory = Path(directory)
    try:
        report = json.loads((directory / "build-report.json").read_text())
    except (OSError, ValueError) as error:
        raise ValueError("Application archive identity is missing; rebuild it") from error
    if (report.get("model") != model or report.get("display_variant") != display or
            report.get("profile") != ("standalone" if standalone else "legacy")):
        raise ValueError("Archive model, display variant or profile differs from this build")
    root = Path(__file__).resolve().parents[1]
    inputs = {
        "source_sha256": set((root / "src").rglob("*.c")),
        "header_sha256": set((root / "include").rglob("*.h")),
        "platform_source_sha256": {p for p in (root / "platform").glob("*.cpp")
                                    if p.name != "firmware_main.cpp"},
    }
    for group, paths in inputs.items():
        hashes = report.get(group)
        if not isinstance(hashes, dict) or set(hashes) != {str(p.relative_to(root)) for p in paths}:
            raise ValueError("Archive source inventory is incomplete or differs; rebuild it")
        for name, expected in hashes.items():
            if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
                raise ValueError("Archive input changed; rebuild before linking: " + name)
