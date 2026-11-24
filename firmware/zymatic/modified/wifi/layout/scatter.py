"""Exercise fragmented storage of the extended Zymatic website under AVR simulation."""

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib

import extended
import tail_budget
from network.compact import compose
from oled import compact as oled
from tail_budget import usb_stock, build

HERE = Path(__file__).resolve().parent
ASSETS = ("zymatic_pair_js", "zymatic_site", "zymatic_favicon")


def arrange(original, toolchain, root, runtime):
    stock = root / "original.bin"
    stock.write_bytes(original)
    network, windows = compose(original, toolchain, root)
    network, display_windows = oled.compose(network)
    windows.extend(display_windows)
    network, keyboard_windows = oled.compose_keyboard(network, toolchain, root)
    windows.extend(keyboard_windows)
    dis = subprocess.check_output([toolchain / "avr-objdump", "-D", "-z", "-b", "binary",
                                   "-m", "avr:51", stock], text=True)
    # The new network parser uses existing parts of the Wi-Fi routine as well as both holes.
    # Keep all three routines, branch receivers and the timer display hooks out of the allocation.
    excluded = [(0x2ed0, 0x3228), (0x33a0, 0x3622), (0xccba, 0xcf38),
                (0x5044, 0x504a), (0x8738, 0x873e), (oled.TRANSFER, oled.END), (0xa74c, 0xaa50)]
    # The simulator models the external device at a few fixed instruction addresses,
    # including the original brew runner's bare RET at 0xfd4c.
    harness = (runtime / "tests/simavr_overlay.c").read_text()
    for pc in re.findall(r"avr->pc\s*=\s*(0x[0-9a-f]+)", harness):
        excluded.append((int(pc, 16), int(pc, 16) + 2))
    sequences = [(pc, end) for pc, end in tail_budget.return_sequences(original, dis)
                 if all(pc >= high or end <= low for low, high in excluded)]
    tails = tail_budget.shared_tails(original, sequences)
    tails.sort(key=lambda t: (-(t[1] - t[0] - len(t[3])), t[0]))
    payload = b""
    for name in ASSETS:
        header = (runtime / "site" / (name + ".h")).read_text()
        payload += bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", header))
    packed = bytearray(network)
    index = []
    used = []
    previous = 0
    for pc, end, receiver, branch in tails:
        start = pc + len(branch)
        count = min(end - start, len(payload) - previous)
        if not count:
            break
        packed[pc:start] = branch
        packed[start:start + count] = payload[previous:previous + count]
        previous += count
        index.append((previous, start // 2))
        used.append((pc, end, receiver, branch))
    if previous != len(payload) or b"".join(
            packed[word*2:word*2+end-start] for (end, word), start in zip(index, [0] + [x[0] for x in index])
    ) != payload:
        raise ValueError("Unable to preserve every byte of the compressed website")
    return bytes(packed), payload, index, used, windows


def install_reader(runtime, index):
    path = runtime / "zymatic_api.c"
    src = path.read_text()
    replacements = {
        "#define SCRIPT_BYTE(i) pgm_read_byte_near(zymatic_pair_js+(i))":
            "#include \"site/scatter.h\"\n#define SCRIPT_BYTE(i) candidate_site_byte(i)",
        '#define ZYMATIC_SITE_SECTION __attribute__((section(".low_web"),used))':
            "#define ZYMATIC_SITE_SECTION",
        "uint32_t start = n->route == FAVICON ? pgm_get_far_address(zymatic_favicon) : pgm_get_far_address(zymatic_site);\n            return pgm_read_byte_far(start + offset);":
            "return candidate_site_byte(sizeof(zymatic_pair_js) + offset +\n"
            "                    (n->route == FAVICON ? sizeof(zymatic_site) : 0));",
    }
    for old, new in replacements.items():
        if src.count(old) != 1:
            raise ValueError("Extended site integration changed")
        src = src.replace(old, new)
    path.write_text(src)
    rows = []
    previous = 0
    for i, (end, _) in enumerate(index):
        width = end - previous
        if rows and rows[-1][2] == width:
            rows[-1] = (end, rows[-1][1], width)
        else:
            rows.append((end, i, width))
        previous = end
    if len(index) > 255 or any(width > 255 for _, _, width in rows):
        raise ValueError("Fragment index requires more than one byte")
    runs = ",\n".join(f"    {{{end}, {first}, {width}}}" for end, first, width in rows)
    words = ", ".join(str(word) for _, word in index)
    template = HERE / "scatter_reader.h.in"
    (runtime / "site/scatter.h").write_text(template.read_text().replace("@RUNS@", runs).replace("@WORDS@", words))
    return len(rows) * 4 + len(index) * 2


def simulator(root, runtime, original, packed, asset):
    # The unmodified composition code checks its source family, checksummed original and all release fields.
    # Apply the candidate's additional ROM substitutions to its temporary synthetic image.
    command = [sys.executable, "-c",
               "import sys,pathlib; sys.path.insert(0,'tools'); import zymatic_build as b; "
               "p=pathlib.Path(sys.argv[1]); image,_=b.build_application((p/'original.bin').read_bytes(),"
               "bytes(range(32)),'0a'*16,'brewery.lan:8080'); (p/'base.bin').write_bytes(image)", str(root)]
    subprocess.run(command, cwd=runtime, check=True)
    result = bytearray((root / "base.bin").read_bytes())
    for i, (old, new) in enumerate(zip(original, packed)):
        if old != new:
            if result[i] != old:
                raise ValueError(f"Extended and network or asset patches overlap at {i:#x}")
            result[i] = new
    manifest = json.loads((runtime / "extension/extension.json").read_text())
    symbols = manifest["symbols"]
    struct.pack_into("<H", result, symbols["provision_legacy_sum"], build.legacy_sum(result))
    struct.pack_into("<I", result, len(result) - 4, zlib.crc32(result[:-4]))
    binary, base = root / "extended.bin", root / "original.bin"
    binary.write_bytes(result)
    flags = os.environ.get("SIMAVR_FLAGS")
    if not flags:
        flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "simavr"], text=True)
    def harness(source):
        target = root / source.stem
        subprocess.run([os.environ.get("CC", "cc"), "-O2", source, *shlex.split(flags), "-o", target], check=True)
        return target
    subprocess.run([harness(runtime / "tests/simavr_tables.c"), base, binary], check=True, timeout=60)
    api = harness(runtime / "tests/simavr_overlay.c")
    args = [api, binary, str(symbols["zymatic_hub_host"] - 0x800000),
            str(symbols["__extension_bss_end"] - 0x800000), str(symbols["zymatic_hub_authority"] - 0x800000),
            str(symbols["zymatic_image_invalid"])]
    for case in ("api", "corrupt", "factory-good", "factory-bad", "display", "program-status",
                 "program-history", "about-pairing", "program-display", "menu-encoder", "dialog-encoder", "program-encoder"):
        subprocess.run([*args, case, base], check=True, timeout=60)
    asset_file = root / "assets.bin"
    asset_file.write_bytes(asset)
    subprocess.run([harness(HERE / "scatter_read.c"), binary, asset_file, str(symbols["candidate_site_byte"])],
                   check=True, timeout=60)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--oem-file", type=Path, required=True)
    parser.add_argument("--with-listener", action="store_true", help="Link and simulate all radio transport components")
    args = parser.parse_args()
    original = usb_stock.decode_application(usb_stock.unpack_source(args.oem_file.read_bytes()))
    if not usb_stock.is_original_app(original):
        raise ValueError("Expected original 1.1.14 application")
    toolchain = args.toolchain.resolve()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        runtime = extended.stage(root)
        packed, asset, index, tails, windows = arrange(original, toolchain, root, runtime)
        index_size = install_reader(runtime, index)
        sys.path.insert(0, str(HERE / "http"))
        from literals import stage as pack_literals
        pack_literals(runtime)
        compiler = runtime / "tools/compile_extension.py"
        source = compiler.read_text()
        compiler.write_text(source.replace("entry_points = [*build.OFFLINE,",
                                           'entry_points = ["candidate_site_byte", *build.OFFLINE,'))
        if args.with_listener:
            from extended_listener import link
            packed = link(root, runtime, toolchain, windows, packed)
        else:
            subprocess.run([sys.executable, compiler, "--toolchain", toolchain, "--out-dir", runtime / "extension"], check=True)
        tail_budget.check(root, root / "original.bin", original, tails)
        manifest = simulator(root, runtime, original, packed, asset)
        low_free = build.tables.POOL_END - build.tables.SEEK_END - manifest["low_bytes"]
        high_free = build.LIMIT - build.START - manifest["extension_bytes"]
        print(f"{len(asset)} site bytes in {len(index)} fragments with a {index_size}-byte index. "
              f"The extended application has {low_free} bytes free in low flash and {high_free} in high flash. "
              f"Reclaimed {sum(end-start for start,end in windows)} bytes in the network, display and keyboard.")


if __name__ == "__main__":
    main()
