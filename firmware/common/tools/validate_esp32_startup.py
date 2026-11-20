#!/usr/bin/env python3
"""Compile and exercise the shared checked ESP32 initialization owner."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain',type=Path,required=True)
    parser.add_argument('--arduino',type=Path,required=True)
    parser.add_argument('--report',type=Path,default=ROOT/'analysis/esp32-startup-validation.json')
    args=parser.parse_args();sdk=args.arduino.resolve();toolchain=args.toolchain.resolve()
    pins=ROOT.parent/'picostill/reverse-engineered/dependencies'
    for folder,pin in ((sdk,pins/'arduino-esp32.json'),(toolchain,pins/'toolchain.json')):
        for name,expected in json.loads(pin.read_text())['identity_sha256'].items():
            if digest(folder/name)!=expected:raise ValueError('Pinned identity mismatch: '+name)
    subprocess.run([sys.executable,'-m','unittest','discover','-s',str(ROOT/'tests'),'-p','test_esp32_startup.py','-v'],check=True)
    compiler=toolchain/'bin/xtensa-esp32-elf-g++'
    flags=['-std=gnu++11','-Os','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fno-exceptions','-fno-rtti','-mlongcalls','-ffunction-sections','-fdata-sections',
           '-DESP_PLATFORM','-DESP32','-DARDUINO_ARCH_ESP32','-DARDUINO=10805','-DF_CPU=240000000L','-DCORE_DEBUG_LEVEL=0','-DHAVE_CONFIG_H']
    includes=[ROOT/'platform/esp32',sdk/'cores/esp32',sdk/'variants/esp32']+sorted(p for p in (sdk/'tools/sdk/include').iterdir() if p.is_dir())
    output=ROOT/'build/esp32/startup.cpp.o';output.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(compiler),*flags,*['-I'+str(p) for p in includes],'-c',str(ROOT/'platform/esp32/startup.cpp'),'-o',str(output)],check=True)
    undefined=subprocess.check_output([str(toolchain/'bin/xtensa-esp32-elf-nm'),'-u',str(output)],text=True)
    if 'erase' in undefined:raise ValueError('Checked startup unexpectedly references an erase operation')
    inputs=[ROOT/'platform/esp32/startup.cpp',ROOT/'platform/esp32/startup.hpp',ROOT/'tests/startup_native.cpp',ROOT/'tests/test_esp32_startup.py',Path(__file__).resolve(),*sorted((ROOT/'tests/startup_shim').glob('*'))]
    report=dict(schema_version=1,native_checks=45,pinned_target_compiled=True,hardware_tested=False,compiler_sha256=digest(compiler),flags=flags,
                object_sha256=digest(output),oem_sdk_init_source_sha256=digest(sdk/'cores/esp32/esp32-hal-misc.c'),
                scope='Preserve pinned CPU/PSRAM/log/NVS-init/BT/init/initVariant order; return exact NVS status without erase or retry. Caller must inhibit appliance startup on any error.',
                inputs={p.relative_to(ROOT).as_posix():digest(p) for p in inputs})
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('Checked ESP32 startup compiled with pinned SDK; no NVS erase reference')
if __name__=='__main__':main()
