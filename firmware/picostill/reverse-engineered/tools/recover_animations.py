#!/usr/bin/env python3
"""Recover the 39 OEM LED pattern/repeat records referenced by 0x400d5dbc."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parents[2] / "tools/firmware_analysis"))
from esp_image import mapped_offset, parse_esp32
INPUT_SHA = "99ac1244a17b72dab51af1f61ed461cee5e85e050c898fc84518fcaeedadc7fc"


def outputs():
    raw = (ROOT.parent / "oem/upstream/picostill_0_0_35.bin").read_bytes()
    image = parse_esp32(raw)
    if image["sha256"] != INPUT_SHA:
        raise ValueError("Wrong OEM image")
    records = []
    for index in range(39):
        offset = mapped_offset(image, 0x3f4129d0+index*8, 8)
        repeat, pointer = struct.unpack_from("<iI", raw, offset)
        start = mapped_offset(image, pointer)
        end = raw.find(b"\0", start, start+64)
        if end < 0:
            raise ValueError("Unterminated original LED program")
        mapped_offset(image, pointer, end-start+1)
        text = raw[start:end].decode("ascii")
        if any(c not in "gry.-" for c in text):
            raise ValueError("Unknown OEM LED instruction")
        records.append({"index": index, "repeat": repeat, "address": pointer, "text": text})
    source = ['// Original PicoStill 0.0.35 LED table, verified by tools/recover_animations.py.\n',
              '#include "picostill/hardware.hpp"\n\nnamespace picostill { namespace oem {\n',
              'const AnimationProgram* animation_program(Animation animation) noexcept {\n',
              '    static const AnimationProgram programs[] = {\n']
    source += ['        {%s, %d},\n' % (json.dumps(r['text']), r['repeat']) for r in records]
    source += ['    };\n    const auto index=static_cast<std::uint32_t>(animation);\n',
               '    return index<AnimationCount ? &programs[index] : nullptr;\n}\n',
               '}} // namespace picostill::oem\n']
    catalog = {"input_sha256": INPUT_SHA, "table_address": 0x3f4129d0,
               "evidence_function": 0x400d5dbc, "records": records}
    return {"analysis/animations.json": json.dumps(catalog, indent=2)+"\n",
            "src/animations.cpp": "".join(source)}


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args=parser.parse_args()
    for name, text in outputs().items():
        path=ROOT / name
        if args.check:
            if not path.is_file() or path.read_text()!=text: raise SystemExit("Stale animation source: "+name)
        else: path.write_text(text)
    print("Verified 39 original LED programs")
