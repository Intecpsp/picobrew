#!/usr/bin/env python3
"""Extract the six complete OEM browser asset literals without altering bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parents[2] / "tools/firmware_analysis"))
from esp_image import mapped_offset, parse_esp32

INPUT_SHA = "99ac1244a17b72dab51af1f61ed461cee5e85e050c898fc84518fcaeedadc7fc"
ASSETS = {"data.css": 0x3f401c6f, "data.js": 0x3f4037a5, "data.fragment.html": 0x3f40aa8f,
          "wifi.css": 0x3f40c3b7, "wifi.js": 0x3f40d62a, "wifi.fragment.html": 0x3f40f3ed}


def outputs():
    raw = (ROOT.parent / "oem/upstream/picostill_0_0_35.bin").read_bytes()
    if hashlib.sha256(raw).hexdigest() != INPUT_SHA:
        raise ValueError("Wrong OEM image")
    image = parse_esp32(raw)
    result, records = {}, []
    for name, address in ASSETS.items():
        start = mapped_offset(image, address)
        end = raw.find(b"\0", start, start+65536)
        if end < 0:
            raise ValueError("Missing asset terminator")
        mapped_offset(image, address, end-start+1)
        data = raw[start:end]
        data.decode("ascii")
        result["web/oem/"+name] = data
        records.append({"path": "web/oem/"+name, "address": address, "file_offset": start,
                        "size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    manifest = {"input_sha256": INPUT_SHA, "assets": records,
                "scope": "Verbatim OEM asset literals. HTML files are fragments assembled by the original HTTP handler, not standalone pages. No claim of new authenticated API support.",
                "modified": False}
    result["analysis/web-assets.json"] = (json.dumps(manifest, indent=2)+"\n").encode()
    header = '// Generated from preserved PicoStill 0.0.35 browser literals.\n#pragma once\nnamespace picostill { namespace oem { namespace web {\n'
    for group in ("data", "wifi"):
        source = '#include "picostill/web_assets.hpp"\nnamespace picostill { namespace oem { namespace web {\n'
        for suffix, symbol in (("css", "css"), ("js", "js"), ("fragment.html", "fragment")):
            name = group+"_"+symbol
            header += 'extern const char '+name+'[];\n'
            content = result['web/oem/'+group+'.'+suffix].decode('ascii')
            source += 'const char '+name+'[] =\n'
            source += '\n'.join(json.dumps(line) for line in content.splitlines(keepends=True))+';\n'
        source += '}}} // namespace picostill::oem::web\n'
        result['src/web_'+group+'.cpp'] = source.encode()
    header += '}}} // namespace picostill::oem::web\n'
    result['include/picostill/web_assets.hpp'] = header.encode()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for name, content in outputs().items():
        path = ROOT / name
        if args.check:
            if not path.is_file() or path.read_bytes() != content:
                raise SystemExit("Stale OEM web asset: "+name)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
    print("Verified six unmodified OEM browser asset literals")
