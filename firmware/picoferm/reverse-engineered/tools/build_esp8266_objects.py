#!/usr/bin/env python3
"""Compile all PicoFerm application modules and actual SDK drivers; see the separate full-link tool."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain',type=Path,required=True);parser.add_argument('--arduino',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'build/esp8266');args=parser.parse_args()
    for name,path in (('toolchain',args.toolchain),('arduino-esp8266',args.arduino)):
        pin=json.loads((ROOT/'dependencies'/f'{name}.json').read_text())
        for rel,sha in pin['identity_sha256'].items():
            if digest(path/rel)!=sha:raise ValueError('Dependency identity mismatch: '+rel)
    compiler=args.toolchain/'bin/xtensa-lx106-elf-g++'
    flags=['-std=c++11','-Os','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-mlongcalls',
           '-mtext-section-literals','-ffunction-sections','-fdata-sections','-falign-functions=4',
           '-fno-fast-math','-ffp-contract=off','-D__ets__','-DICACHE_FLASH','-U__STRICT_ANSI__',
           '-DESP8266','-DARDUINO_ARCH_ESP8266','-DARDUINO=10805','-DF_CPU=80000000L']
    includes=[ROOT/'include',ROOT/'platform/esp8266',args.arduino/'cores/esp8266',args.arduino/'variants/generic',
              args.arduino/'libraries/ESP8266WiFi/src',args.arduino/'libraries/DNSServer/src',args.arduino/'libraries/ESP8266httpUpdate/src',args.arduino/'libraries/ESP8266HTTPClient/src',args.arduino/'libraries/Wire',args.arduino/'libraries/EEPROM',args.arduino/'tools/sdk/include',args.arduino/'tools/sdk/libc/xtensa-lx106-elf/include']
    args.output.mkdir(parents=True,exist_ok=True);sources=[];objects=[]
    for source in sorted((ROOT/'src').glob('*.cpp'))+sorted((ROOT/'platform/esp8266').glob('*.cpp')):
        rel=source.relative_to(ROOT).as_posix();out=args.output/(rel.replace('/','_')+'.o')
        subprocess.run([str(compiler)]+flags+['-I'+str(p) for p in includes]+['-c',str(source),'-o',str(out)],check=True)
        sources.append(dict(source=rel,source_sha256=digest(source),object_sha256=digest(out),object_bytes=out.stat().st_size));objects.append(str(out))
    archive=args.output/'libpicoferm_recovered_esp8266.a'
    if archive.exists():archive.unlink()
    subprocess.run([str(args.toolchain/'bin/xtensa-lx106-elf-ar'),'rcs',str(archive)]+objects,check=True)
    report=dict(schema_version=1,compiler=subprocess.check_output([str(compiler),'--version'],text=True).splitlines()[0],
        compiler_sha256=digest(compiler),flags=flags,sources=sources,archive_sha256=digest(archive),build_script_sha256=digest(__file__),
        headers={p.relative_to(ROOT).as_posix():digest(p) for base in ('include','platform') for p in sorted((ROOT/base).rglob('*.hpp'))},
        dependencies={p.name:digest(p) for p in sorted((ROOT/'dependencies').glob('*.json'))},complete_application_linked=False,hardware_tested=False,
        scope='Pinned Xtensa LX106 GCC 4.8.2 and Arduino ESP8266 2.4.1 object compilation of the complete application and concrete SDK drivers. Object report only: complete ELF/map linkage is recorded separately. No flash/install package.')
    (args.output/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Compiled %d PicoFerm target modules; no firmware image produced'%len(sources))

if __name__=='__main__':main()
