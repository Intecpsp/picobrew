#!/usr/bin/env python3
"""Build and smoke-test one redistributable USB worker on its target OS/CPU."""

import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = Path(__file__).resolve().parent
AVR_RELEASE = "https://github.com/avrdudes/avrdude/releases/download/v8.1/"
AVR_SOURCE = ("avrdude-8.1.tar.gz", "2d3016edd5281ea09627c20b865e605d4f5354fe98f269ce20522a5b910ab399")
AVR = {
    "darwin-arm64": AVR_SOURCE,
    "darwin-amd64": ("avrdude_v8.1_macOS_64bit.tar.gz", "d7739fbb5d1fe649511121a695dac3f4ca5ccb348919bf1f45f9bc5a2ea0ce72"),
    "linux-amd64": ("avrdude_v8.1_Linux_64bit.tar.gz", "c751c88b1c0b886834d85cd4b19f100cc3415c896ab9f98cf7e2955edbcd678f"),
    "linux-arm64": ("avrdude_v8.1_Linux_ARM64.tar.gz", "9e1f2c1e7988bac93f30e5e8aea6cd7a9c8e782542f4d93dc04b6495820184d8"),
    "linux-arm": ("avrdude_v8.1_Linux_ARMv6.tar.gz", "174343ea5c4c3b0d29e98eb4c8de44e0f075a407fded755a1b7fcf793909d1da"),
    "windows-amd64": ("avrdude-v8.1-windows-x64.zip", "e4d571d81fee3387d51bfdedd0b6565e4c201e974101cac2caec7adfd6201da3"),
}


def target():
    arch = {"x86_64":"amd64", "amd64":"amd64", "aarch64":"arm64", "arm64":"arm64",
            "armv7l":"arm", "armv8l":"arm"}.get(platform.machine().lower())
    if struct.calcsize("P") == 4 and arch == "arm64":
        arch = "arm"
    key = platform.system().lower() + "-" + str(arch)
    if key not in AVR:
        raise ValueError("Unsupported USB worker platform: " + key)
    return key


def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def native(work, output, key, cache):
    name, digest = AVR[key]
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / name
    if not archive.is_file():
        with urllib.request.urlopen(AVR_RELEASE+name, timeout=120) as response, tempfile.NamedTemporaryFile(dir=cache, delete=False) as f:
            shutil.copyfileobj(response, f)
        copy = Path(f.name)
        if sha(copy) != digest:
            copy.unlink()
            raise ValueError("Downloaded AVRDUDE digest differs from the release")
        copy.replace(archive)
    if sha(archive) != digest:
        raise ValueError("Cached AVRDUDE artifact failed its pinned SHA-256")
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive) as zipped:
            zipped.extractall(work)
    else:
        with tarfile.open(archive) as tar:
            tar.extractall(work, filter="data")
    (output/"bin").mkdir(parents=True)
    (output/"etc").mkdir()
    if (name, digest) == AVR_SOURCE:
        src = work/"avrdude-8.1"
        build = work/"avr-build"
        opts = [f"-D{flag}=OFF" for flag in ("HAVE_LIBELF","HAVE_LIBUSB","HAVE_LIBUSB_1_0","HAVE_LIBHIDAPI","HAVE_LIBFTDI","HAVE_LIBFTDI1","HAVE_LIBSERIALPORT","HAVE_LIBREADLINE")]
        subprocess.run(["cmake","-S",str(src),"-B",str(build),"-DCMAKE_POLICY_VERSION_MINIMUM=3.5","-DCMAKE_BUILD_TYPE=Release","-DCMAKE_DISABLE_FIND_PACKAGE_SWIG=TRUE",*opts],check=True)
        subprocess.run(["cmake","--build",str(build),"--config","Release","--parallel","4"],check=True)
        executable = next(p for p in build.rglob("avrdude") if p.is_file() and os.access(p,os.X_OK))
        conf = next(build.rglob("avrdude.conf"))
        license_file = src/"COPYING"
    else:
        executable = next(work.rglob("avrdude.exe" if key.startswith("windows") else "avrdude"))
        conf = next(work.rglob("avrdude.conf"))
        license_file = next(p for p in work.rglob("*") if p.is_file() and p.name.lower().startswith(("license","copying")))
        if key.startswith("windows"):
            for dll in executable.parent.glob("*.dll"):
                shutil.copy2(dll, output/"bin"/dll.name)
    shutil.copy2(executable, output/"bin"/executable.name)
    shutil.copy2(conf, output/"etc"/"avrdude.conf")
    # On Windows the upstream build also searches the executable directory.
    if key.startswith("windows"):
        shutil.copy2(conf, output/"bin"/"avrdude.conf")
    shutil.copy2(license_file, output/"AVRDUDE-LICENSE.txt")


def project_files():
    # Exact runtime resources. Never walk private captures, local credentials, or build directories.
    patterns = [
        "tools/private_files.py", "tools/firmware_analysis/esp_image.py", "server/tools/usb/cc_service.py", "firmware/upstream-manifest.json", "firmware/releases.json",
        "firmware/common/tools/esp_usb.py", "firmware/common/tools/esp_stock_partitions.py",
        "firmware/z-series/modified/tools/usb_profile.py",
        "firmware/zymatic/modified/extension/extension.bin", "firmware/zymatic/modified/extension/extension.json",
        "firmware/zymatic/modified/site/*.h","firmware/zymatic/modified/src/*.c","firmware/zymatic/modified/src/*.S","firmware/zymatic/modified/src/*.ld",
        "firmware/zymatic/modified/zymatic_api*.c","firmware/zymatic/modified/zymatic_api*.h",
        "firmware/pico-s-pro/modified/firmware/standalone/pico-hub-recovery.bin",
        "firmware/pico-s-pro/modified/firmware/standalone/pico-s/pico-s-standalone.bin",
        "firmware/pico-s-pro/modified/firmware/standalone/pico-pro/pico-pro-standalone.bin",
        "firmware/pico-c/modified/firmware/standalone/pico-c/*/pico-c-standalone.bin",
    ]
    for model in ("pico-s-pro","pico-c"):
        patterns.extend(f"firmware/{model}/modified/firmware/web/{part}" for part in ("*.gz","manifest.json"))
    patterns.extend("firmware/pico-s-pro/modified/tools/"+n+".py" for n in
                    ("pico_uart_probe","pico_flash_backup_v2","pico_flash_install","pico_flash_preflight","pico_inspect_backup","pico_private_files","provision_api_key"))
    patterns.extend("firmware/zymatic/modified/tools/"+n+".py" for n in ("usb_stock","usb_avr","usb_application","build","offline_program"))
    release_index = json.loads((ROOT/"firmware/releases.json").read_text())["releases"]
    for model in ("picostill","picoferm","z-series"):
        latest = max((r for r in release_index if r["model"]==model and r["kind"]=="reference"),key=lambda r:tuple(map(int,r["firmware_version"].split("."))))
        patterns.append(f"firmware/{model}/modified/releases/{model}/{latest['firmware_version']}.zip")
    # Include further inputs covered by the Zymatic extension's source fingerprint.
    extra = [*ROOT.glob("firmware/zymatic/modified/src/*.h"), *ROOT.glob("firmware/zymatic/modified/zymatic_history.h"),
             *ROOT.glob("firmware/zymatic/modified/tools/boot_evidence.py")]
    files = set(extra)
    for pattern in patterns:
        matches = list(ROOT.glob(pattern))
        if not matches:
            raise ValueError("Missing USB package input: " + pattern)
        files.update(matches)
    return sorted(files)


def build(args):
    key = target()
    with tempfile.TemporaryDirectory(prefix="picobrew-usb-") as d:
        work = Path(d)
        avr = work/"native"
        native(work/"archive", avr, key, args.cache.resolve())
        paths = project_files()
        cmd = [sys.executable,"-m","PyInstaller","--clean","--noconfirm","--onefile","--name","picobrew-usb",
               "--distpath",str(work/"dist"),"--workpath",str(work/"pyi"),"--specpath",str(work)]
        for mod in ("cc3200tool","esptool","serial","usb","libusb_package"):
            cmd.extend(["--collect-all",mod])
        # These protocol modules are loaded from their original firmware tree.
        # Include their standard-library imports in the frozen interpreter.
        std = set()
        for path in paths:
            if path.suffix==".py":
                for node in ast.walk(ast.parse(path.read_text(encoding="utf8"))):
                    if isinstance(node,ast.Import):
                        std.update(n.name for n in node.names if n.name.split(".")[0] in sys.stdlib_module_names)
                    if isinstance(node,ast.ImportFrom) and node.module and node.module.split(".")[0] in sys.stdlib_module_names:
                        std.add(node.module)
            cmd.extend(["--add-data",str(path)+":"+str(path.parent.relative_to(ROOT))])
        for mod in sorted(std-{"msvcrt","fcntl","termios","tty","pty"}):
            cmd.extend(["--hidden-import",mod])
        for path in avr.rglob("*"):
            if path.is_file():
                opt = "--add-binary" if path.parent.name=="bin" and path.suffix!=".conf" else "--add-data"
                cmd.extend([opt,str(path)+":"+str(Path("native")/path.parent.relative_to(avr))])
        cmd.append(str(SCRIPT/"usb_bridge.py"))
        subprocess.run(cmd,check=True)
        built = next((work/"dist").iterdir())
        result = subprocess.run([str(built),"--self-test"],capture_output=True,text=True,timeout=120)
        if result.returncode or json.loads(result.stdout).get("type")!="complete":
            raise ValueError("Frozen USB helper failed its offline dependency and firmware self-test: " + result.stderr[-3500:])
        args.output.mkdir(parents=True,exist_ok=True)
        file=args.output/("picobrew-usb-"+key+(".exe" if key.startswith("windows") else ""))
        shutil.copy2(built,file)
        meta = {"platform":key,"name":file.name,"bytes":file.stat().st_size,"sha256":sha(file)}
        (args.output/(key+".json")).write_text(json.dumps(meta,indent=2)+"\n")
        print(json.dumps(meta))


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--cache",type=Path,default=Path.home()/".cache"/"picobrew-usb-build")
    build(parser.parse_args())
