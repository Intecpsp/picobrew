#!/usr/bin/env python3
"""Exercise reference packaging against real complete model builds.

--builds is a private/local JSON list of {model,elf,build_report,source_root}
records. Its paths are not copied into the public evidence report. Each case
is assembled twice, then corruption and stale-build rejections are exercised.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

import package_reference as package

ROOT=Path(__file__).resolve().parents[1]

def reject(action):
    try:action()
    except (ValueError,OSError):return
    raise AssertionError('Invalid reference package was accepted')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--builds',type=Path,required=True)
    parser.add_argument('--esp8266-sdk',type=Path,required=True)
    parser.add_argument('--esp32-sdk',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'build/reference-validation')
    parser.add_argument('--report',type=Path,default=ROOT/'analysis/reference-package-validation.json')
    args=parser.parse_args();cases=json.loads(args.builds.read_text());results=[]
    if not cases or len({c['model'] for c in cases})!=len(cases):raise ValueError('Provide distinct model builds')
    args.output.mkdir(parents=True,exist_ok=True)
    for case in cases:
        destination=args.output/case['model'];destination.mkdir(exist_ok=True)
        marker=destination/'unrelated.txt';marker.write_text('preserve this user file\n')
        inputs=(case['model'],Path(case['elf']),Path(case['build_report']),args.esp8266_sdk,
                None if case['model']=='picoferm' else args.esp32_sdk,destination,Path(case['source_root']))
        first=package.build(*inputs);first_manifest=(destination/'manifest.json').read_bytes()
        second=package.build(*inputs)
        assert (destination/'manifest.json').read_bytes()==first_manifest and first==second
        assert marker.read_text()=='preserve this user file\n'
        application=destination/'application.bin';saved=application.read_bytes();broken=bytearray(saved);broken[-1]^=1
        application.write_bytes(broken);reject(lambda:package.verify_package(destination));application.write_bytes(saved)
        altered=json.loads(first_manifest);altered['components'][0]['offset']+=4096
        (destination/'manifest.json').write_text(json.dumps(altered));reject(lambda:package.verify_package(destination))
        (destination/'manifest.json').write_bytes(first_manifest)
        with tempfile.TemporaryDirectory(prefix='package-stale-',dir=args.output) as temporary:
            report=json.loads(Path(case['build_report']).read_text());report['elf_sha256']='0'*64
            path=Path(temporary)/'bad-build.json';path.write_text(json.dumps(report))
            stale=(inputs[0],inputs[1],path,*inputs[3:])
            reject(lambda:package.build(*stale))
        assert (destination/'manifest.json').read_bytes()==first_manifest
        package.verify_package(destination)
        results.append(dict(model=case['model'],profile=first['profile'],source_entries_verified=first['source_entries_verified'],
                            elf_sha256=first['elf_sha256'],build_report_sha256=first['build_report_sha256'],
                            manifest_sha256=hashlib.sha256(first_manifest).hexdigest(),
                            components=first['components'],deterministic_refresh=True,unrelated_file_preserved=True,
                            corrupt_image_rejected=True,changed_address_rejected=True,stale_build_rejected_without_replacement=True,
                            image_bytes_match_elf=True,filesystem_remount_and_independent_decode=True))
    # A correctly checksummed table with a different layout must still fail.
    available=next((r for r in results if r['model']!='picoferm'),None)
    geometry_checked=False
    if available:
        data=bytearray((args.output/available['model']/'partitions.bin').read_bytes())
        data[2*32+4]^=1
        end=32*len(package.PARTITIONS);data[end+16:end+32]=hashlib.md5(data[:end]).digest()
        reject(lambda:package.verify_partition_table(bytes(data)));geometry_checked=True
    inputs=['tools/package_reference.py','tools/build_esp8266_image.py','tools/validate_reference_packages.py',
            'tools/build_spiffs_image.py','tools/spiffs_format.py','tools/spiffs_image_native.cpp','dependencies/reference-images.json',
            'dependencies/spiffs-format-abi.json']
    report=dict(schema_version=1,models=[c['model'] for c in cases],all_three_models=set(c['model'] for c in cases)==set(package.MODELS),
                hardware_access_performed=False,installation_qualified=False,wrong_geometry_with_valid_md5_rejected=geometry_checked,
                cases=results,source_sha256={name:package.digest(ROOT/name) for name in inputs},
                image_parser_sha256=package.digest(package.REPOSITORY/'tools/firmware_analysis/esp_image.py'),
                scope='Real complete application ELFs and build reports; deterministic offline packages, every allocated image byte bound to ELF, exact partition geometry and filesystem readback, corruption/stale-input rejection. No hardware installation or runtime qualification.')
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(models=report['models'],all_three_models=report['all_three_models'],passed=True)))
if __name__=='__main__':main()
