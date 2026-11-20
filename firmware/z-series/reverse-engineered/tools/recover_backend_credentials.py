#!/usr/bin/env python3
"""Materialize original shared protocol constants only inside ignored build output.

The preserved, hash-pinned OEM image is the source of these bytes. This generator
never prints them and does not introduce a new per-device credential scheme.
"""
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path
from analyze_images import parse_image
ROOT=Path(__file__).resolve().parents[1]
IMAGE_SHA='d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27'
AUTHORIZATION_ADDRESS=0x3f412fd5
AZURE_KEY_ADDRESS=0x3f42a4dc
AZURE_KEY_BYTES=32
# Original live HTTP diagnostic resources, including URL-bearing assertions.
# Keep this address-only map public; all bytes remain in ignored build output.
HTTP_CLIENT_TEST_ADDRESSES=(
    0x3f411b78,0x3f4130b9,0x3f413124,0x3f41318e,0x3f41320d,
    0x3f413260,0x3f4132b2,0x3f41332f,0x3f413368,0x3f4133b3,
    0x3f4133e0,0x3f413436,0x3f413446,0x3f413469,0x3f41349b,
    0x3f4134ce,0x3f4134e9,0x3f413516,0x3f413517,0x3f41351f,
    0x3f41354f,0x3f4135a0,0x3f4135bc,0x3f418735,
)

def generate(output:Path):
    original=(ROOT.parent/'oem/upstream/zseries_0_0_119.bin').read_bytes()
    if hashlib.sha256(original).hexdigest()!=IMAGE_SHA:raise ValueError('Original image identity mismatch')
    segments=parse_image(original)['segments']
    def mapped(address,size):
        for segment in segments:
            if segment['load_address']<=address and address+size<=segment['load_address']+segment['size_bytes']:
                offset=segment['file_offset']+address-segment['load_address'];return original[offset:offset+size]
        raise ValueError('Unmapped credential span')
    authorization=mapped(AUTHORIZATION_ADDRESS,256).split(b'\0',1)[0]
    if not authorization or len(authorization)==256:raise ValueError('Unterminated original authorization')
    key=mapped(AZURE_KEY_ADDRESS,AZURE_KEY_BYTES)
    lines=['// Generated from preserved Z119 into build output; never commit this file.','#pragma once','#include <cstdint>','namespace zseries { namespace oem { namespace original_credentials {']
    lines.append('static const char http_authorization[] = {'+','.join(str(b) for b in authorization+b'\0')+'};')
    lines.append('static const std::uint8_t azure_key[] = {'+','.join(str(b) for b in key)+'};')
    resource_hashes={}
    for address in HTTP_CLIENT_TEST_ADDRESSES:
        value=mapped(address,512).split(b'\0',1)[0]
        if not value or len(value)==512:raise ValueError('Unterminated HTTP diagnostic resource')
        resource_hashes[hex(address)]=hashlib.sha256(value).hexdigest()
        lines.append('static const char http_test_'+format(address,'08x')+'[] = {'+','.join(str(b) for b in value+b'\0')+'};')
    lines.append('inline const char* http_client_test_text(void*, std::uint32_t address) { switch(address) {')
    lines.append('case '+hex(AUTHORIZATION_ADDRESS)+': return http_authorization;')
    lines.extend('case '+hex(address)+': return http_test_'+format(address,'08x')+';' for address in HTTP_CLIENT_TEST_ADDRESSES)
    lines.append('default: return nullptr; } }')
    lines.append('}}} // namespace zseries::oem::original_credentials')
    output.mkdir(parents=True,exist_ok=True);header=output/'oem_credentials.hpp';header.write_text('\n'.join(lines)+'\n');header.chmod(0o600)
    return dict(original_image_sha256=IMAGE_SHA,authorization_address=hex(AUTHORIZATION_ADDRESS),azure_key_address=hex(AZURE_KEY_ADDRESS),azure_key_bytes=AZURE_KEY_BYTES,
                http_client_test_resource_sha256=resource_hashes,
                generated_header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=ROOT/'build/generated');args=parser.parse_args()
    report=generate(args.output);(args.output/'credential-provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Generated original backend constants in build output; values not printed')
if __name__=='__main__':main()
