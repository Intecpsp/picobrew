"""Bounded Cortex-M execution of the pinned Pico C OEM image.

Only the generic emulator machinery is reused from the preserved S/Pro tools.
The image being executed is always a separately hash-verified Pico C payload.
"""
import hashlib
import importlib.util
import os
from pathlib import Path

from inspect_oem import REPO, VARIANTS, read_variant

ROOT = Path(__file__).resolve().parents[1]
VARIANT = os.environ.get('PICO_C_OEM_VARIANT', 'ssd1325')
if VARIANT not in VARIANTS:
    raise ValueError('Unknown Pico C OEM variant: ' + VARIANT)
IMAGE_SHA256 = VARIANTS[VARIANT]['raw_sha256']
DEFAULT_IMAGE = ROOT / 'build' / VARIANTS[VARIANT]['file']
IMAGE_BASE = 0x20004000

_path = REPO / 'firmware/pico-s-pro/reverse-engineered/tools/original_arm.py'
_spec = importlib.util.spec_from_file_location('_pico_c_bounded_arm_engine', _path)
_engine = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_engine)
words_from_double = _engine.words_from_double
double_from_words = _engine.double_from_words


class ArmFirmware(_engine.ArmFirmware):
    def __init__(self, image=None, verify=True):
        if image is None:
            raw = read_variant(VARIANT)
            DEFAULT_IMAGE.parent.mkdir(parents=True, exist_ok=True)
            if not DEFAULT_IMAGE.exists() or DEFAULT_IMAGE.read_bytes() != raw:
                DEFAULT_IMAGE.write_bytes(raw)
            image = DEFAULT_IMAGE
        if not verify or hashlib.sha256(Path(image).read_bytes()).hexdigest() != IMAGE_SHA256:
            raise ValueError('Expected the selected, hash-pinned Pico C OEM payload')
        super().__init__(image=image, verify=False)
