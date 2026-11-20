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
    args = parser.parse_args()
    subprocess.run([sys.executable, RECOVERY/'verify_import.py'], check=True)
    out = ROOT/'build/hub-recovery'
    sdk = Path(os.environ.get('PICOBREW_SDK', ROOT/'.deps/energia')).resolve()
    prefix = os.environ.get('PICOBREW_ARM_PREFIX', str(ROOT/'.deps/toolchain/bin/arm-none-eabi-'))
    sources = [RECOVERY/'recovery/recovery.c', RECOVERY/'recovery/sha256.c',
               ROOT/'platform/hub_recovery/http.c', ROOT/'platform/hub_recovery/identity.cpp',
               RECOVERY/'recovery/web_assets.c', RECOVERY/'src/http_server.c']
    command = [sys.executable, RECOVERY/'tools/build_sdk_validation.py', '--recovery',
               '--entry', RECOVERY/'recovery/native.cpp', '--sdk-root', sdk, '--arm-prefix', prefix,
               '--output-dir', out, '--linker', RECOVERY/'platform/pico_cc3200.ld',
               '--include', RECOVERY/'recovery']
    for source in sources:
        command.extend(['--source', source])
    subprocess.run(command, check=True)
    raw = out/'sdk_validation.bin'
    layout = inspect_image(out/'sdk_validation.elf',raw)
    print(json.dumps({'image': str(raw),'bytes':raw.stat().st_size,
                      'sha256':hashlib.sha256(raw.read_bytes()).hexdigest(), 'layout':layout}))
    if args.check and raw.read_bytes() != (ROOT/'firmware/standalone/pico-hub-recovery.bin').read_bytes():
        raise SystemExit('Hub recovery differs from the packaged image')

if __name__ == '__main__':
    main()
