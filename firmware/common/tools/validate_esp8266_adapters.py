#!/usr/bin/env python3
"""Verify raw SPIFFS persistence and asynchronous lwIP transport ownership."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import esp8266_adapters_native as native

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arduino',type=Path,required=True)
    parser.add_argument('--i386',type=Path)
    parser.add_argument('--write-report',action='store_true')
    args=parser.parse_args();native.verify_sdk(args.arduino)
    with tempfile.TemporaryDirectory(prefix='picobrew-esp8266-adapter-evidence-') as directory:
        results={kind:native.build_and_run(Path(directory)/kind,args.arduino,kind,args.i386)
                 for kind in ('storage','telemetry')}
    files=['include/picobrew/verified_storage.hpp','src/verified_storage.cpp',
           'platform/esp8266/storage.hpp','platform/esp8266/storage.cpp',
           'platform/esp8266/telemetry.hpp','platform/esp8266/telemetry.cpp',
           'tests/esp8266_storage_test.cpp','tests/esp8266_telemetry_test.cpp',
           'tests/esp8266_sdk_host/arch/cc.h','tests/esp8266_adapters_native.py',
           'tests/test_esp8266_adapters.py','tools/validate_esp8266_adapters.py',
           'tools/build_esp8266_adapters.py','dependencies/esp8266-adapters.json']
    report=dict(schema_version=1,compiler=subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
        results=results,source_sha256={name:native.digest(ROOT/name) for name in files},
        sdk_pin='dependencies/esp8266-adapters.json',
        storage_boundary='Actual pinned SPIFFS C implementation and owned raw mount/FileIo/VerifiedStorage run against NOR flash read/write/erase callbacks backed by deterministic memory. Flash programming uses bitwise AND; erase geometry is checked. No Arduino FS wrapper or automatic format is used by the production adapter.',
        telemetry_boundary='Actual owned transport and unmodified pinned lwIP public headers execute against explicit DNS/TCP/pbuf service callbacks. Native arch/cc.h supplies fixed-width wire types without ESP8266 ROM declarations. DNS cancellation/late callback, PCB errors, copied partial writes, deferred output, pbuf ownership/backpressure/EOF, and close are tested. No live DNS/network/cloud is contacted.',
        hardware_tested=False,original_oem_parity_claimed=False)
    if args.i386:
        report['i386_runtime']='GCC 14 multilib with pinned repository recipe-i386 sysroot and qemu-i386; pointer/long widths explicitly measured in results'
        pin=ROOT.parent/'z-series/reverse-engineered/tests/toolchains/recipe-i386.json'
        report['i386_runtime_pin_sha256']=native.digest(pin)
    if args.write_report:
        path=ROOT/'analysis/esp8266-adapters-validation.json';path.parent.mkdir(exist_ok=True)
        path.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
