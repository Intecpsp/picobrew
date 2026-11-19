"""Supported S/Pro build identities and archive compatibility checks."""
import json

MODELS = {"pico-pro": "PICO_MODEL_PICO_PRO", "pico-s": "PICO_MODEL_PICO_S"}
DEFAULT_MODEL = "pico-pro"


def add_model_argument(parser):
    parser.add_argument("--model", choices=tuple(MODELS), default=DEFAULT_MODEL,
                        help="Physical S/Pro variant (default: pico-pro); other machines need their own port")


def model_flag(model):
    return "-DPICO_MODEL=" + MODELS[model]


def verify_archive_model(directory, model, standalone):
    """Fail before linking when --skip-archive-build selects another profile."""
    try:
        report = json.loads((directory / "build-report.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError("Application archive identity is missing; rebuild without --skip-archive-build") from error
    if report.get("model") != model or report.get("profile") != ("standalone" if standalone else "legacy"):
        raise ValueError("Application archive model/profile differs from this build; rebuild without --skip-archive-build")
