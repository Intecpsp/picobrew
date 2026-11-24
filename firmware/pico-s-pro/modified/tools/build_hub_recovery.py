"""Link the existing independent recovery application with authenticated Hub identity."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RECOVERY = ROOT / 'recovery'
sys.path.insert(0, str(RECOVERY / 'tools'))
from build_recovery import inspect_image

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Compare against the shipped bootstrap image')
    parser.add_argument('--display', choices=('ssd1325', 'ssd1309'), help='Build a Pico C recovery for this display')
    args = parser.parse_args()
    subprocess.run([sys.executable, RECOVERY/'verify_import.py'], check=True)
    out = ROOT/'build/hub-recovery'
    sdk = Path(os.environ.get('PICOBREW_SDK', ROOT/'.deps/energia')).resolve()
    prefix = os.environ.get('PICOBREW_ARM_PREFIX', str(ROOT/'.deps/toolchain/bin/arm-none-eabi-'))
    sources = [RECOVERY/'recovery/recovery.c', RECOVERY/'recovery/sha256.c',
               ROOT/'platform/hub_recovery/http.c', ROOT/'platform/hub_recovery/identity.cpp',
               RECOVERY/'recovery/web_assets.c', RECOVERY/'src/http_server.c']
    destination = ROOT/'firmware/standalone/pico-hub-recovery.bin'
    if args.display:
        croot = ROOT.parents[1]/'pico-c/modified'
        out = croot/'build/hub-recovery'/args.display
        out.mkdir(parents=True, exist_ok=True)
        wrapper = out/'variant_http.c'
        wrapper.write_text(f'#define PICO_DISPLAY_VARIANT "{args.display}"\n#include "variant_recovery.c"\n')
        sources[2] = wrapper
        destination = croot/'firmware/standalone/pico-c'/args.display/'pico-c-recovery.bin'
    command = [sys.executable, RECOVERY/'tools/build_sdk_validation.py', '--recovery',
               '--entry', RECOVERY/'recovery/native.cpp', '--sdk-root', sdk, '--arm-prefix', prefix,
               '--output-dir', out, '--linker', RECOVERY/'platform/pico_cc3200.ld',
               '--include', RECOVERY/'recovery']
    if args.display:
        command += ['--include', croot/'platform/hub_recovery']
    for source in sources:
        command.extend(['--source', source])
    subprocess.run(command, check=True)
    raw = out/'sdk_validation.bin'
    layout = inspect_image(out/'sdk_validation.elf',raw)
    print(json.dumps({'image': str(raw),'bytes':raw.stat().st_size,
                      'sha256':hashlib.sha256(raw.read_bytes()).hexdigest(), 'layout':layout}))
    if args.check and raw.read_bytes() != destination.read_bytes():
        raise SystemExit('Hub recovery differs from the packaged image')

if __name__ == '__main__':
    main()
