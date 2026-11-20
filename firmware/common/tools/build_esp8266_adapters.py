#!/usr/bin/env python3
"""Compile shared storage/telemetry and actual pinned ESP8266 SPIFFS drivers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain',type=Path,required=True)
    parser.add_argument('--arduino',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'build/esp8266-adapters')
    args=parser.parse_args()
    pin_path=ROOT/'dependencies/esp8266-adapters.json';pin=json.loads(pin_path.read_text())
    for base,key in ((args.arduino,'sdk_identity_sha256'),(args.toolchain,'toolchain_identity_sha256')):
        for relative,expected in pin[key].items():
            if digest(base/relative)!=expected:
                raise ValueError('Pinned dependency mismatch: '+relative)
    compiler=args.toolchain/'bin/xtensa-lx106-elf-g++'
    flags=['-Os','-Wall','-Wextra','-Werror','-mlongcalls','-mtext-section-literals',
           '-ffunction-sections','-fdata-sections','-falign-functions=4','-fno-fast-math',
           '-ffp-contract=off','-D__ets__','-DICACHE_FLASH','-U__STRICT_ANSI__','-DESP8266',
           '-DARDUINO_ARCH_ESP8266','-DARDUINO=10805','-DF_CPU=80000000L','-DLWIP_OPEN_SRC']
    cpp_flags=['-std=c++11','-fno-exceptions','-fno-rtti']
    includes=[ROOT/'include',ROOT/'platform/esp8266',args.arduino/'cores/esp8266',
              args.arduino/'variants/generic',args.arduino/'tools/sdk/include',
              args.arduino/'tools/sdk/lwip/include',args.arduino/'tools/sdk/libc/xtensa-lx106-elf/include']
    args.output.mkdir(parents=True,exist_ok=True)
    own=['src/verified_storage.cpp','platform/esp8266/storage.cpp','platform/esp8266/telemetry.cpp']
    dependencies=['cores/esp8266/spiffs_hal.cpp']+[
        'cores/esp8266/spiffs/'+name+'.c' for name in
        ('spiffs_hydrogen','spiffs_nucleus','spiffs_cache','spiffs_gc','spiffs_check')]
    sources=[];objects=[]
    for sdk,relative in [(False,p) for p in own]+[(True,p) for p in dependencies]:
        source=(args.arduino if sdk else ROOT)/relative
        out=args.output/(('sdk_' if sdk else '')+relative.replace('/','_')+'.o')
        cpp=source.suffix=='.cpp'
        command=[str(compiler if cpp else args.toolchain/'bin/xtensa-lx106-elf-gcc')]+flags
        if cpp:command+=cpp_flags
        if sdk:command+=['-Wno-unused-parameter','-Wno-sign-compare']
        subprocess.run(command+['-I'+str(p) for p in includes]+['-c',str(source),'-o',str(out)],check=True)
        sources.append(dict(source=relative,dependency=sdk,source_sha256=digest(source),object_sha256=digest(out),object_bytes=out.stat().st_size))
        objects.append(str(out))
    archive=args.output/'libpicobrew_esp8266_adapters.a'
    if archive.exists():archive.unlink()
    subprocess.run([str(args.toolchain/'bin/xtensa-lx106-elf-ar'),'rcs',str(archive)]+objects,check=True)
    symbols=subprocess.check_output([str(args.toolchain/'bin/xtensa-lx106-elf-nm'),'-g','--defined-only',str(args.arduino/'tools/sdk/lib/liblwip_gcc.a')],text=True)
    required=['dns_gethostbyname','ipaddr_aton','tcp_new','tcp_arg','tcp_recv','tcp_sent','tcp_poll','tcp_err',
              'tcp_connect','tcp_abort','tcp_close','tcp_recved','tcp_write','tcp_output','pbuf_free','pbuf_copy_partial']
    defined={line.split()[-1] for line in symbols.splitlines() if len(line.split())>=3}
    missing=sorted(set(required)-defined)
    if missing:raise ValueError('SDK lwIP symbols missing: '+','.join(missing))
    report=dict(schema_version=1,compiler=subprocess.check_output([str(compiler),'--version'],text=True).splitlines()[0],
        compiler_sha256=digest(compiler),flags=flags,cpp_flags=cpp_flags,sources=sources,
        archive_sha256=digest(archive),build_script_sha256=digest(__file__),dependency_pin_sha256=digest(pin_path),
        headers={p.relative_to(ROOT).as_posix():digest(p) for p in
                 [ROOT/'include/picobrew/verified_storage.hpp',ROOT/'platform/esp8266/storage.hpp',ROOT/'platform/esp8266/telemetry.hpp']},
        lwip_link_symbols=required,complete_application_linked=False,hardware_tested=False,
        scope='Pinned ESP8266 2.4.1/GCC 4.8.2 object compilation of shared transaction, raw SPIFFS mount/driver, nonblocking lwIP telemetry and six actual SDK SPIFFS/HAL objects. lwIP API symbol availability is checked against the pinned archive. Full family image linking is separate.')
    (args.output/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Compiled three owned adapters and six SDK objects; all 16 lwIP link symbols exist')

if __name__=='__main__':main()
