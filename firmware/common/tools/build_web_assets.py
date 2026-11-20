#!/usr/bin/env python3
"""Compile the flash-streamed common website with both pinned Xtensa compilers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--esp8266-toolchain',type=Path,required=True)
    parser.add_argument('--esp8266-sdk',type=Path,required=True)
    parser.add_argument('--esp32-toolchain',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    flags=['-std=c++11','-Os','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-mlongcalls','-mtext-section-literals','-ffunction-sections','-fdata-sections']
    report={'schema_version':1,'source_sha256':sha(ROOT/'src/common_web_assets.cpp'),'headers':{str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'include/picobrew/common_web.h',ROOT/'include/picobrew/http_server.h']},'targets':[],'complete_application_linked':False,'hardware_tested':False}
    with tempfile.TemporaryDirectory(prefix='pico-web-target-') as directory:
        for name,root,sdk,extra in [('esp8266',args.esp8266_toolchain,args.esp8266_sdk,['-D__ets__','-DICACHE_FLASH','-U__STRICT_ANSI__','-DESP8266']),('esp32',args.esp32_toolchain,None,[])]:
            prefix='xtensa-lx106-elf' if name=='esp8266' else 'xtensa-esp32-elf'
            compiler=root/'bin'/f'{prefix}-g++'
            includes=['-I'+str(ROOT/'include')]
            if sdk:includes+=['-I'+str(sdk/'cores/esp8266'),'-I'+str(sdk/'tools/sdk/include')]
            obj=Path(directory)/(name+'.o')
            subprocess.run([str(compiler),*flags,*extra,*includes,'-c',str(ROOT/'src/common_web_assets.cpp'),'-o',str(obj)],check=True)
            symbols=subprocess.check_output([str(root/'bin'/f'{prefix}-objdump'),'-t',str(obj)],text=True)
            assets=[line for line in symbols.splitlines() if 'L7asset_' in line and 'O' in line.split()[:3]]
            assert len(assets)==6,assets
            if name=='esp8266':assert all('.irom.text' in line for line in assets),assets
            report['targets'].append({'target':name,'compiler':subprocess.check_output([str(compiler),'--version'],text=True).splitlines()[0],'compiler_sha256':sha(compiler),'flags':flags+extra,'object_bytes':obj.stat().st_size,'object_sha256':sha(obj),'asset_sections':assets,'sdk_headers':{'cores/esp8266/pgmspace.h':sha(sdk/'cores/esp8266/pgmspace.h')} if sdk else {}})
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('Common assets compiled for ESP8266 and ESP32; six ESP8266 arrays verified in flash')

if __name__=='__main__':main()
