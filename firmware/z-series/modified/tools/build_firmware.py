#!/usr/bin/env python3
"""Build the standalone Z ELF and checked reference application image.

No partition geometry, bootloader or physical compatibility is assumed here.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import struct

ROOT=Path(__file__).resolve().parents[1]
OEM=ROOT.parent/'reverse-engineered'
COMMON=ROOT.parents[1]/'common'
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for option in ('toolchain','arduino','arduinojson','ethernet2','output'):
        parser.add_argument('--'+option,type=Path,required=True)
    parser.add_argument('--objects',type=Path,help='Reuse an already checked OEM object directory')
    args=parser.parse_args()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    objects=args.objects.resolve() if args.objects else output/'oem-objects'
    if not args.objects:
        subprocess.run([sys.executable,str(OEM/'tools/build_esp32_objects.py'),
            '--toolchain',str(args.toolchain),'--arduino',str(args.arduino),
            '--arduinojson',str(args.arduinojson),'--ethernet2',str(args.ethernet2),
            '--output',str(objects)],check=True)
    command=[sys.executable,str(OEM/'tools/link_esp32_application.py'),
        '--toolchain',str(args.toolchain),'--arduino',str(args.arduino),
        '--objects',str(objects),'--output',str(output/'link'),
        '--entry',str(ROOT/'platform/esp32/application_main.cpp'),'--profile','standalone']
    for folder in [ROOT/'include',ROOT/'platform/esp32',COMMON/'include',COMMON/'src',COMMON/'platform/esp32']:
        command+=['--extra-include',str(folder)]
    sources=sorted((COMMON/'src').glob('*.cpp'))+[COMMON/'src/http_server.c']
    sources+=sorted((COMMON/'platform/esp32').glob('*.cpp'))+sorted((ROOT/'src').glob('*.cpp'))
    sources += [ROOT/'platform/esp32/msp_bus.cpp']
    for source in sources:command+=['--extra-source',str(source)]
    subprocess.run(command,check=True)
    report=json.loads((output/'link/link-report.json').read_text())
    esptool=args.arduino/'tools/esptool.py'
    application=output/'application.bin'
    subprocess.run([sys.executable,str(esptool),'--chip','esp32','elf2image',
        '--flash_mode','dio','--flash_freq','40m','--flash_size','4MB','-o',str(application),str(output/'link/application.elf')],check=True)
    spec=importlib.util.spec_from_file_location('pinned_esptool',esptool)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    with application.open('rb') as stream:image=module.ESP32FirmwareImage(stream)
    if image.checksum!=image.calculate_checksum() or not image.append_digest or image.calc_digest!=image.stored_digest:
        raise ValueError('Generated application image checksum/digest mismatch')
    if image.flash_mode!=2 or image.flash_size_freq!=0x20 or application.stat().st_size>0x1f0000:
        raise ValueError('Application image does not fit the explicit 4-MiB reference configuration')
    layout_path=output/'target-memory-layout.bin'
    subprocess.run([str(args.toolchain/'bin/xtensa-esp32-elf-objcopy'),
        '--dump-section','.rodata.picobrew_z_memory='+str(layout_path),str(output/'link/application-entry.o')],check=True)
    layout=struct.unpack('<6I',layout_path.read_bytes())
    report['target_memory_bytes']=dict(zip(('application_owner','task_stack','core_owner','head_owner','adapter','engine_handle'),layout))
    report['application_image']={'path':'application.bin','sha256':digest(application),'bytes':application.stat().st_size,
        'entrypoint':hex(image.entrypoint),'flash_mode':'dio','flash_frequency_mhz':40,'reference_flash_bytes':0x400000,
        'reference_partition_offset':0x10000,'reference_partition_bytes':0x1f0000,
        'checksum_verified':True,'appended_sha256_verified':True,'esptool_sha256':digest(esptool),
        'segments':[{'address':hex(segment.addr),'bytes':len(segment.data),'sha256':hashlib.sha256(segment.data).hexdigest()} for segment in image.segments]}
    report['flash_image_produced']=True
    report['installation_qualified']=False
    report['device_model']='z-series'
    report['variant_mapping']='Z1/Z2 mapping has not been established from an original device backup'
    report['task_stack_bytes']=32768
    report['scope']+=' The standalone profile replaces remote service stages with authenticated local API/history/telemetry and a local recipe catalogue. Persistent storage is checked without automatic formatting. A latched output inhibit and bounded MSP bus transactions are modified policies. Stack frame sizes are compiler estimates, not measured runtime stack or heap high-water marks.'
    report['build_script_sha256']=digest(__file__)
    (output/'standalone-build-report.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
