#!/usr/bin/env python3
"""Encode the pinned Arduino ESP8266 eboot/application recipe from real ELFs.

This follows Arduino 2.4.1 platform.txt: eboot .text, 0xAA padding to 4096,
then application .irom0.text/.text/.data/.rodata with an Espressif XOR footer.
No serial operation, binary patch, inferred code or fabricated OEM input.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
REPOSITORY=ROOT.parents[1]
sys.path.insert(0,str(REPOSITORY/'tools'))
from firmware_analysis.esp_image import parse_esp8266_arduino

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def slice_checked(data,offset,size):
    if offset<0 or size<0 or offset>len(data) or size>len(data)-offset:raise ValueError('ELF data lies outside its file')
    return data[offset:offset+size]

def read_elf(path):
    data=Path(path).read_bytes()
    header=struct.unpack('<16sHHIIIIIHHHHHH',slice_checked(data,0,52))
    ident,kind,machine,version,entry,_,table,_,header_size,_,_,stride,count,names_index=header
    if ident[:7]!=b'\x7fELF\x01\x01\x01' or kind!=2 or machine!=94 or version!=1 or header_size!=52 or stride!=40:
        raise ValueError('Expected a little-endian 32-bit Xtensa executable ELF')
    if not count or count>4096 or names_index>=count:raise ValueError('Invalid ELF section table')
    records=[struct.unpack('<10I',slice_checked(data,table+i*stride,stride)) for i in range(count)]
    strings=records[names_index]
    if strings[1]!=3:raise ValueError('ELF section-name table is not a string table')
    names=slice_checked(data,strings[4],strings[5]);sections={}
    for record in records:
        name,section_type,flags,address,offset,size,_,_,_,_=record
        if name>=len(names) or b'\0' not in names[name:]:raise ValueError('Unterminated ELF section name')
        name=names[name:names.index(b'\0',name)].decode('ascii')
        if section_type==1 and flags&2 and size:
            if name in sections or address+size>0x100000000:raise ValueError('Duplicate or overflowing allocated ELF section')
            sections[name]=dict(address=address,data=slice_checked(data,offset,size),flags=flags)
    return entry,sections

def component(entry,sections):
    if not 1<=len(sections)<=16:raise ValueError('Invalid image segment count')
    output=bytearray(struct.pack('<BBBBI',0xe9,len(sections),2,0x30,entry)) # DIO, 40 MHz, 2 MiB reference
    checksum=0xef
    for section in sections:
        payload=section['data']
        if len(payload)%4:raise ValueError('Arduino image recipe requires word-aligned section lengths')
        output.extend(struct.pack('<II',section['address'],len(payload)));output.extend(payload)
        for byte in payload:checksum^=byte
    output.extend(b'\0'*((15-len(output))%16));output.append(checksum)
    return bytes(output)

def verify_elf_image(elf,data,image):
    """Independently bind every allocated PROGBITS byte and entry to the ELF.

    ESP32's converter splits/merges RAM sections around mapped flash segments,
    so this checks address coverage instead of assuming one segment per section.
    """
    entry,sections=read_elf(elf)
    if image['entry_address']!=entry:raise ValueError('Application entry point differs from its ELF')
    for name,section in sections.items():
        offset=0
        while offset<len(section['data']):
            address=section['address']+offset
            matches=[s for s in image['segments'] if s['load_address']<=address<s['load_address']+s['size_bytes']]
            if len(matches)!=1:raise ValueError('ELF section is not uniquely covered by image: '+name)
            segment=matches[0];within=address-segment['load_address'];length=min(segment['size_bytes']-within,len(section['data'])-offset)
            start=segment['file_offset']+within
            if data[start:start+length]!=section['data'][offset:offset+length]:raise ValueError('Image payload differs from ELF section: '+name)
            offset+=length
    return len(sections)

def build(elf,arduino,output):
    elf=Path(elf);arduino=Path(arduino);output=Path(output)
    pin=ROOT.parent/'picoferm/reverse-engineered/dependencies/arduino-esp8266.json'
    for name,expected in json.loads(pin.read_text())['identity_sha256'].items():
        if digest(arduino/name)!=expected:raise ValueError('Pinned ESP8266 SDK mismatch: '+name)
    image_pin=json.loads((ROOT/'dependencies/reference-images.json').read_text())['profiles']['esp8266']
    for name,expected in image_pin['identity_sha256'].items():
        if digest(arduino/name)!=expected:raise ValueError('Pinned ESP8266 image input mismatch: '+name)
    boot=arduino/'bootloaders/eboot/eboot.elf'
    boot_entry,boot_sections=read_elf(boot);app_entry,app_sections=read_elf(elf)
    order=('.irom0.text','.text','.data','.rodata')
    if set(boot_sections)!={'.text'} or set(app_sections)!=set(order):
        raise ValueError('Allocated ELF sections differ from the pinned Arduino image recipe')
    if app_sections['.irom0.text']['address']!=0x40201010:raise ValueError('ELF does not use the selected 2-MiB Arduino reference mapping')
    boot_image=component(boot_entry,[boot_sections['.text']])
    if len(boot_image)>4096:raise ValueError('Pinned eboot no longer fits one sector')
    data=boot_image+b'\xaa'*(4096-len(boot_image))+component(app_entry,[app_sections[name] for name in order])
    if len(data)>0x100000:raise ValueError('Application overlaps the selected filesystem region')
    parsed=parse_esp8266_arduino(data)
    # The independent OEM image parser checks XOR, padding, entries, segment
    # overlap and file bounds. Compare every decoded byte back to each ELF.
    for image,expected in ((parsed['bootloader'],[boot_sections['.text']]),(parsed['application'],[app_sections[n] for n in order])):
        if len(image['segments'])!=len(expected):raise ValueError('Encoded section count changed')
        for segment,section in zip(image['segments'],expected):
            raw=data[segment['file_offset']:segment['file_offset']+segment['size_bytes']]
            if segment['load_address']!=section['address'] or raw!=section['data']:raise ValueError('Encoded image differs from ELF section')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(data)
    return dict(schema_version=1,profile='esp8266-2m',image_bytes=len(data),image_sha256=digest(output),elf_sha256=digest(elf),
                boot_elf_sha256=digest(boot),platform_recipe_sha256=digest(arduino/'platform.txt'),
                linker_sha256=digest(arduino/'tools/sdk/ld/eagle.flash.2m.ld'),encoder_sha256=digest(__file__),
                parser_sha256=digest(REPOSITORY/'tools/firmware_analysis/esp_image.py'),parsed=parsed,
                every_segment_matches_input_elf=True,hardware_tested=False,installation_qualified=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('elf','arduino','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args();report=build(args.elf,args.arduino,args.output)
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('image_bytes','image_sha256','every_segment_matches_input_elf')}))
if __name__=='__main__':main()
