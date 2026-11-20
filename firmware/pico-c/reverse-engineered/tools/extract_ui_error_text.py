#!/usr/bin/env python3
"""Recover the exact localized C error sentences by executing their dispatcher.

The only intercepted operations are display clear and scrolling submission.
Original string composition and code selection execute in both verified images.
Requires the same Unicorn environment as the original-instruction test suite.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_control import OriginalC
from inspect_oem import VARIANTS
from extract_ui_assets import c_string


def generate():
    tables, evidence = {}, []
    for variant in VARIANTS:
        original = OriginalC(variant)
        delta = -0x18 if variant == "ssd1309" else 0
        messages = []
        original.hook(0x20004488, lambda runner: None)
        original.hook(0x20004bd0 + (4 if variant == "ssd1309" else 0),
                      lambda runner: messages.append(runner.cstring(runner.reg(1))))
        rows = []
        for code in [*range(20), 99]:
            row = []
            for language in range(3):
                original.write(0x2002a90c, bytes([language]))
                messages.clear()
                original.call(0x20007214 + delta, [code], max_instructions=100000)
                if len(messages) != 1:
                    raise ValueError("Unexpected C error presentation trace")
                suffix = str(code).encode() + b" at picobrew.com/err"
                if not messages[0].endswith(suffix):
                    raise ValueError("Unexpected C error-code suffix")
                row.append(messages[0][:-len(suffix)] + b"%d at picobrew.com/err")
            rows.append(row)
        tables[variant] = rows
        evidence.append({"variant": variant, "input_sha256": VARIANTS[variant]["raw_sha256"],
                         "entry": f"0x{0x20007214 + delta:08x}", "cases": 63,
                         "text_sha256": hashlib.sha256(b"\0".join(x for row in rows for x in row)).hexdigest()})
    if tables["ssd1325"] != tables["ssd1309"]:
        raise ValueError("C variants disagree on error text")
    source = "/* Exact C error templates, extracted from both original dispatchers. */\n"
    source += "static const char *const error_text[21][3] = {\n"
    for row in tables["ssd1325"]:
        source += "    {" + ", ".join(c_string(x) for x in row) + "},\n"
    source += "};\n"
    report = {"schema_version": 1, "variants": evidence, "original_code_executed": True,
              "hardware_tested": False, "codes": [*range(20), 99],
              "limits": ["Graphics and scrolling are captured boundaries; no physical appliance execution"]}
    return {ROOT / "src/ui/error_text.inc": source,
            ROOT / "analysis/ui-error-text.json": json.dumps(report, indent=2) + "\n"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for path, text in generate().items():
        if args.check:
            if path.read_text() != text:
                raise SystemExit("Recovered error text differs: " + str(path))
        else:
            path.write_text(text)
    print("Verified 126 localized C error presentations across both original images")


if __name__ == "__main__":
    main()
