#!/usr/bin/env python3
"""Assemble checked, reproducible standalone reference installation artifacts.

Input is a complete, source-bound model build and its real ELF. The tool does
not open a serial port or flash/erase a device. Existing generated output can
be refreshed; named artifacts are replaced only after all validation passes.
"""
import argparse
import binascii
import re
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

import build_esp8266_image
import build_spiffs_image
import spiffs_format

ROOT=Path(__file__).resolve().parents[1]
REPOSITORY=ROOT.parents[1]
sys.path.insert(0,str(REPOSITORY/'tools'))
from firmware_analysis.esp_image import parse_esp32,parse_esp8266_arduino

MODELS=('z-series','picostill','picoferm')
PARTITIONS=(('nvs',1,2,0x9000,0x5000),('otadata',1,0,0xe000,0x2000),
            ('factory',0,0,0x10000,0x1f0000),('spiffs',1,0x82,0x200000,0x200000))
PARTITION_CSV='# Explicit standalone 4-MiB reference layout; not an OEM partition table\n# Name,Type,SubType,Offset,Size,Flags\nnvs,data,nvs,0x9000,0x5000,\notadata,data,ota,0xe000,0x2000,\nfactory,app,factory,0x10000,0x1f0000,\nspiffs,data,spiffs,0x200000,0x200000,\n'

STILL_PROTOCOL = 'esp32-factory-v1'
STILL_PARTITIONS = PARTITIONS[:2] + (('pb_recovery',0,0,0x10000,0xf0000),
    ('pb_app',0,0x10,0x100000,0xf0000), PARTITIONS[-1])
STILL_PARTITION_CSV = PARTITION_CSV.replace('factory,app,factory,0x10000,0x1f0000,',
    'pb_recovery,app,factory,0x10000,0xf0000,\npb_app,app,ota_0,0x100000,0xf0000,')

def still_boot_selection():
    sector = 4096
    data = bytearray(b'\xff' * (2 * sector))
    struct.pack_into('<I', data, 0, 1)
    struct.pack_into('<I', data, 28, binascii.crc32(data[:4], 0xffffffff))
    return bytes(data)

def verify_still_descriptor(image, version):
    if not isinstance(version,str) or not re.fullmatch(r'(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*)){2}',version) or len(version)>31:
        raise ValueError('Invalid PicoStill version')
    marker = ('PicoBrew OTA1|picostill|'+version).encode().ljust(64,b'\0')
    if (len(image) > 0xf0000 or len(image) < 0x160 or image[23] != 1 or
            struct.unpack_from('<I', image,24)[0] != 0x3f400020 or struct.unpack_from('<I',image,28)[0]<320 or
            struct.unpack_from('<I', image,32)[0] != 0xabcd5432 or image[0x120:0x160] != marker):
        raise ValueError('PicoStill image has the wrong target, version or native descriptor')

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write_json(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')

def verify_partition_table(data, layout=PARTITIONS):
    if len(data)!=0xc00:raise ValueError('Unexpected generated partition-table length')
    for index,(name,kind,subtype,offset,size) in enumerate(layout):
        record=struct.unpack_from('<HBBII16sI',data,index*32)
        expected=(0x50aa,kind,subtype,offset,size,name.encode().ljust(16,b'\0'),0)
        if record!=expected:raise ValueError('Generated partition geometry does not match the explicit reference')
    end=32*len(layout)
    if data[end:end+16]!=b'\xeb\xeb'+b'\xff'*14 or data[end+16:end+32]!=hashlib.md5(data[:end]).digest():
        raise ValueError('Generated partition-table MD5 mismatch')
    if any(b!=255 for b in data[end+32:]):raise ValueError('Unexpected bytes after partition-table digest')
    return [dict(name=n,type=t,subtype=s,offset=o,bytes=z) for n,t,s,o,z in layout]

def verify_report(model,elf,report_path,source_root,esp8266,esp32):
    report=json.loads(report_path.read_text())
    if report.get('complete_application_linked') is not True or report.get('elf_sha256')!=digest(elf):
        raise ValueError('ELF does not match a complete application build report')
    if not any(key in report for key in ('model','device_model')):
        raise ValueError('Build report must identify its appliance model explicitly')
    for key in ('model','device_model'):
        if key in report and report[key]!=model:raise ValueError('Build report belongs to another appliance model')
    if report.get('link_probe') is True or report.get('profile') not in (None,'standalone'):
        raise ValueError('A link probe or OEM profile is not a standalone reference application')
    if model=='z-series' and (not report.get('reconstructed_sources') or not report.get('reconstructed_headers')):
        raise ValueError('Z-series report must embed reconstructed source and header identities')
    selected=source_root/'firmware'/model
    roots=(source_root,source_root/'firmware',selected/'modified',selected/'reverse-engineered',esp8266,esp32)
    count=0
    def check(name,expected):
        nonlocal count
        path=Path(name)
        if path.is_absolute():
            parts=path.parts
            if 'firmware' not in parts:raise ValueError('Absolute build input cannot be resolved against source root')
            path=Path(*parts[parts.index('firmware'):])
        if '..' in path.parts:raise ValueError('Build report source traversal is invalid')
        candidates=[base/path for base in roots if base is not None]
        if not any(p.is_file() and digest(p)==expected for p in candidates):raise ValueError('Build input changed; rebuild before packaging: '+str(path))
        count+=1
    for key in ('owned_sources','sources','extra_sources','sdk_sources','sdk_objects','reconstructed_sources'):
        for item in report.get(key,[]):
            if isinstance(item,dict) and 'source' in item and 'source_sha256' in item:check(item['source'],item['source_sha256'])
    for key in ('headers','platform_headers','extra_header_sha256','reconstructed_headers'):
        for name,expected in report.get(key,{}).items():check(name,expected)
    if 'entry_sha256' in report:check('platform/esp32/application_main.cpp',report['entry_sha256'])
    script='build_firmware.py' if model=='z-series' else 'build_esp8266.py' if model=='picoferm' else 'build_esp32.py'
    check('tools/'+script,report['build_script_sha256'])
    if not count:raise ValueError('Build report contains no source identity evidence')
    return report,count

def verify_image_inputs(esp8266,esp32):
    pin=json.loads((ROOT/'dependencies/reference-images.json').read_text());checked={}
    for name,base in (('esp8266',esp8266),('esp32',esp32)):
        if base is None:continue
        for path,expected in pin['profiles'][name]['identity_sha256'].items():
            if digest(base/path)!=expected:raise ValueError('Pinned image construction input changed: '+name+'/'+path)
            checked[name+'/'+path]=expected
    return checked

def check_components(components,flash_bytes,model,protocol=None):
    expected={'application.bin':(0,0x100000),'spiffs.bin':(0x100000,0xfb000)} if model=='picoferm' else {
        'bootloader.bin':(0x1000,0x7000),'partitions.bin':(0x8000,0x1000),'application.bin':(0x10000,0x1f0000),'spiffs.bin':(0x200000,0x200000)}
    if protocol:
        if model!='picostill' or protocol!=STILL_PROTOCOL:raise ValueError('Unsupported OTA reference layout')
        expected.update({'recovery.bin':(0x10000,0xf0000),'application.bin':(0x100000,0xf0000),'otadata.bin':(0xe000,0x2000)})
    if model not in MODELS or flash_bytes!=(0x200000 if model=='picoferm' else 0x400000):raise ValueError('Unknown reference layout')
    if len(components)!=len(expected) or {item['file'] for item in components}!=set(expected):raise ValueError('Reference component set changed')
    previous=0
    for item in sorted(components,key=lambda x:x['offset']):
        if (item['offset'],item['maximum_bytes'])!=expected[item['file']]:raise ValueError('Reference component address changed')
        if item['offset']%4096 or item['bytes']<=0 or item['bytes']>item['maximum_bytes'] or item['offset']<previous:
            raise ValueError('Reference artifact overlap, size or alignment is invalid')
        previous=item['offset']+item['maximum_bytes']
        if previous>flash_bytes:raise ValueError('Reference artifact exceeds selected flash size')

def build(model,elf,build_report,esp8266_sdk,esp32_sdk,output,source_root=REPOSITORY):
    if model not in MODELS:raise ValueError('Unsupported reference appliance model')
    elf=Path(elf).resolve();build_report=Path(build_report).resolve();source_root=Path(source_root).resolve()
    esp8266=Path(esp8266_sdk).resolve();esp32=Path(esp32_sdk).resolve() if esp32_sdk else None
    output=Path(output).resolve();esp8266_model=model=='picoferm'
    if not esp8266_model and esp32 is None:raise ValueError('ESP32 models require the pinned ESP32 SDK')
    report,source_count=verify_report(model,elf,build_report,source_root,esp8266,esp32)
    protocol=report.get("ota_protocol")
    inputs=verify_image_inputs(esp8266,esp32)
    profile='esp8266-2m' if esp8266_model else 'esp32-4m'
    flash_bytes=0x200000 if esp8266_model else 0x400000
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.reference-build-',dir=output) as temporary:
        directory=Path(temporary);app=directory/'application.bin';components=[]
        shutil.copyfile(elf,directory/'application.elf');shutil.copyfile(build_report,directory/'build-report.json')
        def component(name,offset,limit,kind):
            file=directory/name
            components.append(dict(file=name,kind=kind,offset=offset,bytes=file.stat().st_size,maximum_bytes=limit,sha256=digest(file)))
        if esp8266_model:
            encoded=build_esp8266_image.build(elf,esp8266,app)
            parsed=parse_esp8266_arduino(app.read_bytes());component('application.bin',0,0x100000,'eboot-and-application')
            partitions=[];preserved=[dict(offset=0x1fb000,bytes=0x5000,purpose='Reference SDK EEPROM/RF/system tail; not included in generated write artifacts')]
        else:
            esptool=esp32/'tools/esptool.py'
            subprocess.run([sys.executable,str(esptool),'--chip','esp32','elf2image','--flash_mode','dio','--flash_freq','40m','--flash_size','4MB','-o',str(app),str(elf)],check=True)
            parsed=parse_esp32(app.read_bytes())
            if parsed['flash_mode']!=2 or parsed['flash_size_frequency_header']!=0x20 or not parsed['digest_verified']:
                raise ValueError('Application header does not describe the selected 4-MiB DIO/40-MHz reference')
            prior=report.get('application_image',report.get('app_image'))
            if prior and prior.get('sha256')!=digest(app):raise ValueError('Re-encoded image differs from the complete build report')
            encoded=dict(encoder='pinned SDK esptool.py elf2image',encoder_sha256=digest(esptool),elf_sha256=digest(elf))
            encoded['elf_sections_verified']=build_esp8266_image.verify_elf_image(elf,app.read_bytes(),parsed)
            shutil.copyfile(esp32/'tools/sdk/bin/bootloader_dio_40m.bin',directory/'bootloader.bin')
            boot=parse_esp32((directory/'bootloader.bin').read_bytes())
            if boot['flash_mode']!=2 or boot['flash_size_frequency_header']!=0x20 or not boot['digest_verified']:
                raise ValueError('Pinned bootloader configuration differs from the explicit reference')
            csv=directory/'partitions.csv';csv.write_text(STILL_PARTITION_CSV if protocol==STILL_PROTOCOL else PARTITION_CSV)
            subprocess.run([sys.executable,str(esp32/'tools/gen_esp32part.py'),str(csv),str(directory/'partitions.bin')],check=True)
            partitions=verify_partition_table((directory/'partitions.bin').read_bytes(), STILL_PARTITIONS if protocol==STILL_PROTOCOL else PARTITIONS)
            component('bootloader.bin',0x1000,0x7000,'pinned-sdk-bootloader');component('partitions.bin',0x8000,0x1000,'explicit-reference-partition-table')
            preserved=[dict(offset=0x9000,bytes=0x5000,purpose='NVS: no blank or replacement credential image is generated')]
            if protocol==STILL_PROTOCOL:
                verify_still_descriptor(app.read_bytes(),report['firmware_version'])
                shutil.copyfile(app,directory/'recovery.bin')
                (directory/'otadata.bin').write_bytes(still_boot_selection())
                component('recovery.bin',0x10000,0xf0000,'immutable-network-recovery')
                component('application.bin',0x100000,0xf0000,'wireless-application')
                component('otadata.bin',0xe000,0x2000,'initial-application-selection')
            else:
                component('application.bin',0x10000,0x1f0000,'factory-application')
                preserved.append(dict(offset=0xe000,bytes=0x2000,purpose='OTA selection region: no write artifact; factory-only reference layout'))
        fs_report=build_spiffs_image.build(profile,esp8266,esp32,directory/'spiffs.bin')
        geometry=spiffs_format.PROFILES[profile];component('spiffs.bin',geometry['partition_offset'],geometry['partition_bytes'],'verified-empty-spiffs')
        check_components(components,flash_bytes,model,protocol)
        write_json(directory/'filesystem-report.json',fs_report)
        write_json(directory/'application-image-report.json',dict(encoding=encoded,decoded=parsed))
        (directory/'README.txt').write_text(
            'PicoBrew standalone reference package: '+model+'\n\n'
            'These are explicit custom/reference artifacts, not an OEM full-device backup.\n'
            'No hardware installation, flash geometry or runtime qualification is inferred.\n'
            'The package tool has not connected to, flashed or erased a device.\n\n'
            'manifest.json names each generated image, exact offset, maximum region and SHA-256.\n'
            'Only those image ranges form the reference write set; no full-chip erase image is supplied.\n'
            'Confirm the actual device flash capacity, wiring and partition contents against a complete\n'
            'private backup before planning installation. Preserve the listed NVS/EEPROM/RF regions.\n'
            'spiffs.bin is a verified empty, formatted filesystem for a clean reference installation.\n'
            'Writing it replaces existing modified settings/history in that region.\n'
            'Firmware startup never formats a failed mount or erases damaged NVS.\n\n'
            'Provision the API key and Wi-Fi through the physical UART; no key/password is supplied.\n'
            'See firmware/'+model+'/modified/README.md for controls, provisioning and model boundaries.\n')
        files=[dict(file=p.name,bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(directory.iterdir()) if p.is_file()]
        manifest=dict(schema_version=1,model=model,profile=profile,reference_flash_bytes=flash_bytes,
                      layout_is_oem=False,hardware_tested=False,installation_qualified=False,hardware_access_performed=False,
                      elf_sha256=digest(elf),build_report_sha256=digest(build_report),source_entries_verified=source_count,
                      sdk_image_inputs_sha256=inputs,package_script_sha256=digest(__file__),
                      reference_pin_sha256=digest(ROOT/'dependencies/reference-images.json'),
                      application_checksum_verified=parsed['checksum_verified'],application_digest_verified=parsed['digest_verified'],
                      filesystem_remount_verified=True,filesystem_independently_decoded=True,filesystem_contains_credentials=False,
                      components=components,partitions=partitions,preserved_regions=preserved,files=files)
        if protocol:manifest.update(ota_protocol=protocol,ota_image='application.bin')
        write_json(directory/'manifest.json',manifest)
        # Manifest is replaced last. An interrupted refresh cannot certify a
        # mixed package: any earlier manifest's component hashes will disagree.
        for path in sorted(directory.iterdir(),key=lambda p:(p.name=='manifest.json',p.name)):
            os.replace(path,output/path.name)
    verify_package(output)
    return manifest

def verify_package(directory):
    directory=Path(directory);manifest=json.loads((directory/'manifest.json').read_text())
    protocol=manifest.get('ota_protocol')
    check_components(manifest['components'],manifest['reference_flash_bytes'],manifest['model'],protocol)
    for item in manifest['files']:
        if Path(item['file']).name!=item['file']:raise ValueError('Package file path is not a single filename')
        path=directory/item['file']
        if path.stat().st_size!=item['bytes'] or digest(path)!=item['sha256']:raise ValueError('Package artifact checksum/length changed: '+item['file'])
    for item in manifest['components']:
        path=directory/item['file']
        if path.stat().st_size!=item['bytes'] or digest(path)!=item['sha256']:raise ValueError('Component metadata differs from its artifact')
    if digest(directory/'application.elf')!=manifest['elf_sha256'] or digest(directory/'build-report.json')!=manifest['build_report_sha256']:
        raise ValueError('Package ELF/build-report identity changed')
    parsed=parse_esp8266_arduino((directory/'application.bin').read_bytes()) if manifest['model']=='picoferm' else parse_esp32((directory/'application.bin').read_bytes())
    if not parsed['checksum_verified']:raise ValueError('Application checksum is invalid')
    if protocol==STILL_PROTOCOL:
        if manifest.get('ota_image')!='application.bin' or (directory/'otadata.bin').read_bytes()!=still_boot_selection():
            raise ValueError('Invalid wireless package selection')
        app=(directory/'application.bin').read_bytes()
        if app!=(directory/'recovery.bin').read_bytes():raise ValueError('Factory recovery differs from verified build')
        report=json.loads((directory/'build-report.json').read_bytes())
        if report.get('ota_protocol')!=protocol or report.get('model')!='picostill':
            raise ValueError('Firmware build report does not implement the declared update protocol')
        verify_still_descriptor(app,report['firmware_version'])
    if manifest['model']!='picoferm':
        table=verify_partition_table((directory/'partitions.bin').read_bytes(), STILL_PARTITIONS if protocol==STILL_PROTOCOL else PARTITIONS)
        if table!=manifest['partitions']:raise ValueError('Manifest partitions do not match the verified table')
        boot=parse_esp32((directory/'bootloader.bin').read_bytes())
        if not boot['digest_verified']:raise ValueError('Bootloader digest is unavailable')
    build_esp8266_image.verify_elf_image(directory/'application.elf',(directory/'application.bin').read_bytes(),parsed['application'] if manifest['model']=='picoferm' else parsed)
    spiffs_format.inspect_image((directory/'spiffs.bin').read_bytes(),manifest['profile'],{})
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',choices=MODELS,required=True)
    for name in ('elf','build-report','esp8266-sdk','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--esp32-sdk',type=Path)
    parser.add_argument('--source-root',type=Path,default=REPOSITORY,help='Source checkout matching the supplied build report')
    args=parser.parse_args()
    try:manifest=build(args.model,args.elf,args.build_report,args.esp8266_sdk,args.esp32_sdk,args.output,args.source_root)
    except (ValueError,OSError,KeyError,subprocess.CalledProcessError) as error:parser.exit(1,'Reference package failed: '+str(error)+'\n')
    print(json.dumps(dict(model=manifest['model'],profile=manifest['profile'],components=len(manifest['components']),source_entries_verified=manifest['source_entries_verified'],hardware_access_performed=False)))
if __name__=='__main__':main()
