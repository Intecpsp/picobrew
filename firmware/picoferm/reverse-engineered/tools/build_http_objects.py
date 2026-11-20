#!/usr/bin/env python3
"""Compile the PicoFerm HTTP implementation and concrete pinned SDK adapter."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--toolchain',type=Path,required=True);parser.add_argument('--arduino',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    for name,path in [('toolchain',args.toolchain),('arduino-esp8266',args.arduino)]:
        for relative,expected in json.loads((ROOT/'dependencies'/f'{name}.json').read_text())['identity_sha256'].items():
            if digest(path/relative)!=expected:raise ValueError('Dependency identity mismatch: '+relative)
    compiler=args.toolchain/'bin/xtensa-lx106-elf-g++';args.output.mkdir(parents=True,exist_ok=True)
    flags=['-std=c++11','-Os','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-mlongcalls','-mtext-section-literals','-ffunction-sections','-fdata-sections','-falign-functions=4','-fno-fast-math','-ffp-contract=off','-D__ets__','-DICACHE_FLASH','-U__STRICT_ANSI__','-DESP8266','-DARDUINO_ARCH_ESP8266','-DARDUINO=10805','-DF_CPU=80000000L']
    includes=[ROOT/'include',ROOT/'platform/esp8266']+[args.arduino/p for p in ['cores/esp8266','variants/generic','libraries/ESP8266WiFi/src','libraries/DNSServer/src','tools/sdk/include','tools/sdk/libc/xtensa-lx106-elf/include']]
    sources=[]
    for relative in ['src/http.cpp','src/http_server.cpp','platform/esp8266/http_ports.cpp']:
        source=ROOT/relative;output=args.output/(relative.replace('/','_')+'.o')
        subprocess.run([str(compiler)]+flags+['-I'+str(p) for p in includes]+['-c',str(source),'-o',str(output)],check=True)
        sources.append(dict(source=relative,source_sha256=digest(source),object_sha256=digest(output),object_bytes=output.stat().st_size))
    report=dict(schema_version=1,compiler=subprocess.check_output([str(compiler),'--version'],text=True).splitlines()[0],compiler_sha256=digest(compiler),flags=flags,sources=sources,
        headers={p:digest(ROOT/p) for p in ['include/picoferm/http.hpp','include/picoferm/http_server.hpp','platform/esp8266/http_ports.hpp','src/http_assets.inc']},
        sdk={p:digest(args.arduino/p) for p in ['libraries/DNSServer/src/DNSServer.h','libraries/DNSServer/src/DNSServer.cpp','libraries/ESP8266WiFi/src/WiFiClient.h','libraries/ESP8266WiFi/src/WiFiClient.cpp','libraries/ESP8266WiFi/src/WiFiServer.h','libraries/ESP8266WiFi/src/WiFiServer.cpp','cores/esp8266/pgmspace.h']},
        build_script_sha256=digest(__file__),complete_application_linked=False,hardware_tested=False,scope='Three real HTTP modules compiled with pinned Xtensa LX106 GCC4.8.2 and Arduino ESP8266 2.4.1; application/SDK link and physical qualification remain separate evidence.')
    (args.output/'http-build.json').write_text(json.dumps(report,indent=2)+'\n');print('Compiled all three PicoFerm HTTP target modules')
if __name__=='__main__':main()
