#!/usr/bin/env python3
"""Build the C standalone entry with native CC3200 startup and inspect its image.

This produces a complete linked firmware candidate, not a hardware acceptance result.
The independent function-test ELF and test fixtures are never inputs to this link.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile
from package_update import encode_update, decode_update
from machine_model import add_model_argument, verify_archive_model

ROOT=Path(__file__).resolve().parents[1]
ORIGIN,END=0x20004000,0x20040000

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def inspect_image(elf_path,bin_path):
    raw=bin_path.read_bytes()
    with elf_path.open('rb') as stream:
        elf=ELFFile(stream)
        if elf['e_machine']!='EM_ARM' or elf.elfclass!=32 or not elf.little_endian:
            raise ValueError('Expected little-endian 32-bit ARM ELF')
        table=elf.get_section_by_name('.symtab')
        symbols={s.name:int(s['st_value']) for s in table.iter_symbols() if s['st_shndx']!='SHN_UNDEF'}
        undefined=[s.name for s in table.iter_symbols() if s.name and s['st_shndx']=='SHN_UNDEF']
        if undefined:raise ValueError('Undefined firmware symbols: '+repr(undefined))
        test_symbols=[name for name in symbols if 'fixture' in name or name=='pico_validation_entry' or name.startswith('sdk_validation_')]
        if test_symbols:raise ValueError('Test implementation in production link: '+repr(test_symbols))
        required=('ResetISR','main','setup','loop','pico_application_setup','pico_application_loop',
                  'pico_application_initialize','pico_application_bind_services','pico_recipe_run_step',
                  'pico_temperature_control_timer_isr','pico_heater_pump_timer_isr','g_pfnVectors','_heap','_eheap','_bss','_ebss')
        missing=[name for name in required if name not in symbols]
        if missing:raise ValueError('Missing appliance entry/dependency: '+repr(missing))
        segments=[]
        for seg in elf.iter_segments():
            if seg['p_type']!='PT_LOAD':continue
            start,size,filesize=int(seg['p_paddr']),int(seg['p_memsz']),int(seg['p_filesz'])
            if start<ORIGIN or start+size>END:raise ValueError('Firmware segment outside CC3200 application RAM')
            if int(seg['p_vaddr'])!=start:raise ValueError('Unexpected load/run relocation')
            segments.append({'start':hex(start),'memory_bytes':size,'file_bytes':filesize})
        sp,reset=struct.unpack_from('<II',raw)
        if symbols['g_pfnVectors']!=ORIGIN:raise ValueError('Wrong vector-table location')
        if not (ORIGIN<=sp<=END) or sp&7:raise ValueError('Invalid initial stack pointer')
        if not (symbols['_bss']<sp<=symbols['_ebss']):raise ValueError('Initial stack is not inside native SDK BSS stack storage')
        if not reset&1 or (reset&~1)!=(symbols['ResetISR']&~1):raise ValueError('Wrong reset-vector entry')
        if (elf['e_entry']&~1)!=(reset&~1):raise ValueError('ELF and raw reset entries differ')
        if symbols['_eheap']-symbols['_heap']!=32768:raise ValueError('Unexpected heap reservation')
        if symbols['_eheap']>END:raise ValueError('Heap exceeds physical RAM')
        loaded_end=max(int(s['start'],16)+s['file_bytes'] for s in segments if s['file_bytes'])
        if len(raw)!=loaded_end-ORIGIN:raise ValueError('Raw binary extent differs from loadable ELF data')
        return {'passed':True,'load_address':hex(ORIGIN),'ram_end':hex(END),
            'initial_stack_pointer':hex(sp),'reset_vector':hex(reset),'entry':hex(elf['e_entry']),
            'binary_bytes':len(raw),'bss_bytes':symbols['_ebss']-symbols['_bss'],
            'heap_bytes':symbols['_eheap']-symbols['_heap'],'sdk_stack_bytes':32768,
            'unallocated_ram_bytes':END-symbols['_eheap'],'load_segments':segments,
            'undefined_symbols':[],'test_symbols':[],
            'required_entry_symbols':{k:hex(symbols[k]) for k in required}}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    add_model_argument(p)
    p.add_argument('--standalone', action='store_true', default=True,
                   help='Standalone C profile; the preserved OEM build lives separately')
    p.add_argument('--arm-prefix',default=os.environ.get('PICOBREW_ARM_PREFIX'))
    p.add_argument('--sdk-root',type=Path,default=Path(os.environ.get('PICOBREW_SDK',str(ROOT/'.deps/energia'))))
    p.add_argument('--output-dir',type=Path)
    p.add_argument('--skip-archive-build',action='store_true')
    a=p.parse_args()
    out=(a.output_dir or ROOT/'build/standalone'/a.model/a.display/'firmware').resolve()
    out.mkdir(parents=True,exist_ok=True)
    subprocess.run([sys.executable,str(ROOT/'tools/embed_web.py')],check=True)
    common=['--sdk-root',str(a.sdk_root.resolve()),'--standalone','--model',a.model,'--display',a.display]
    if a.arm_prefix:common+=['--arm-prefix',a.arm_prefix]
    if not a.skip_archive_build:
        subprocess.run([sys.executable,str(ROOT/'tools/build.py'),*common,'--archive-only',
                        '--output-dir',str(out/'archives')],check=True)
    archive_report=json.loads((out/'archives/build-report.json').read_text())
    if archive_report.get('display_variant') != a.display:
        raise ValueError('Application archive belongs to a different display variant')
    verify_archive_model(out/'archives',a.model,True,a.display)
    for group in ('source_sha256','header_sha256','platform_source_sha256'):
        for path, expected in archive_report[group].items():
            if digest(ROOT/path) != expected:
                raise ValueError('Archive input changed; rebuild before linking: '+path)
    log=out/'build.log'
    command=[sys.executable,str(ROOT/'tools/build_sdk_validation.py'),*common,
        '--entry',str(ROOT/'platform/firmware_main.cpp'),
        '--linker',str(ROOT/'platform/pico_cc3200.ld'),'--appliance-image','--output-dir',str(out/'native'),
        '--application-lib',str(out/'archives/libpicobrew.a'),
        '--adapter-lib',str(out/'archives/libpicobrew_energia.a')]
    result=subprocess.run(command,capture_output=True,text=True)
    log.write_text(result.stdout+result.stderr)
    if result.returncode:
        print(result.stdout+result.stderr);return result.returncode
    for suffix in ('.elf','.bin','.map'):
        shutil.copy2(out/'native'/('sdk_validation'+suffix),out/('pico-c-standalone'+suffix))
    elf,raw=out/'pico-c-standalone.elf',out/'pico-c-standalone.bin'
    layout=inspect_image(elf,raw)
    forbidden=[b'picobrew.com',b'/API/pico/checkFirmware',b'/API/pico/getFirmware',b'/API/pico/register']
    remaining=[value.decode() for value in forbidden if value in raw.read_bytes()]
    if remaining:
        raise ValueError('Cloud protocol remains in standalone image: '+repr(remaining))
    if raw.read_bytes()[1024:1048] != ('PicoBrew:pico-c:'+a.display+'\0').encode():
        raise ValueError('Missing Pico C OTA target marker after the CC3200 vector table')
    update=out/'pico-c-standalone.update.txt'
    body=encode_update(raw.read_bytes())
    if decode_update(body)!=raw.read_bytes():raise ValueError('Update envelope round trip failed')
    update.write_bytes(body)
    native=json.loads((out/'native/validation.json').read_text())
    compiled=json.loads((out/'native/compile_report.json').read_text())
    import re
    version=re.search(r'^#define PICO_STANDALONE_VERSION "([0-9.]+)"$',
                      (ROOT/'include/pico_c/version.h').read_text(),re.MULTILINE).group(1)
    report={'artifact':'Pico C standalone local API firmware with native Energia/TI startup and runtime',
        'profile':'standalone','model':a.model,'model_family':'pico-c','firmware_version':version,
        'display_variant':a.display,'reference_version':'0.1.34','source_reconstruction':True,'hardware_executed':False,
        'display_geometry':{'width':128,'height':32 if a.display=='ssd1309' else 64,
                            'rotation':0 if a.display=='ssd1309' else 2,'storage_bytes':1024,'transfer_bytes':1024},
        'cloud_protocol_strings_absent':True,'api_key_embedded':False,'firmware_update':False,'hub_ota_protocol':1,
        'browser_manifest_sha256':digest(ROOT/'firmware/web/manifest.json'),
        'browser_storage':json.loads((ROOT/'firmware/web/manifest.json').read_text()),
        'web_sources_sha256':{str(f.relative_to(ROOT)):digest(f) for f in sorted((ROOT/'web').iterdir()) if f.is_file()},
        'production_entry':'platform/firmware_main.cpp','application_composition':'src/application.c',
        'image_layout':layout,'elf_sha256':digest(elf),'binary_sha256':digest(raw),
        'update_body_sha256':digest(update),'update_body_bytes':len(body),
        'sdk_link_passed':native['pass'],'sdk_source_files':native['upstream_sources_compiled'],
        'app_sources_sha256':{str(f.relative_to(ROOT)):digest(f) for directory in ('src','include','platform')
                              for f in sorted((ROOT/directory).rglob('*')) if f.is_file()},
        'compiled_sources':[{'source':c['source'],'sha256':c['source_sha256']} for c in compiled],
        'runtime_boundary':native['runtime_boundary'],
        'validation_scope':'Static image, native ABI and complete link checks. Functional ARM comparisons are reported separately. Physical timing, peripheral operation and recovery/installation are not exercised.',
        'files':{'elf':elf.name,'binary':raw.name,'map':'pico-c-standalone.map','update_body':update.name}}
    (out/'firmware-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'image':str(raw),'binary_bytes':layout['binary_bytes'],
                     'unallocated_ram_bytes':layout['unallocated_ram_bytes'],
                     'image_layout_passed':True,'hardware_executed':False},indent=2))
    return 0
if __name__=='__main__':raise SystemExit(main())
