#!/usr/bin/env python3
"""Acquire the exact Linux x86-64 compiler/SDK references for local firmware builds.

Downloads build dependencies only. It never connects to an appliance or writes
device storage. Every archive and selected extracted identities are pinned.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

COMMON=Path(__file__).resolve().parents[1]
FIRMWARE=COMMON.parent
Z=FIRMWARE/'z-series/reverse-engineered'
FERM=FIRMWARE/'picoferm/reverse-engineered'

def load_acquirer():
    spec=importlib.util.spec_from_file_location('pinned_reference_acquirer',Z/'tools/acquire_esp32.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def package(pin,directory,name):
    value=json.loads(pin.read_text())
    return dict(name=name,directory=directory,url=value['url'],archive=Path(value['url']).name,
                archive_bytes=value['archive_size_bytes'],archive_sha256=value['archive_sha256'],identity_sha256=value['identity_sha256'])

def extract_zip(source,destination,package,acquirer):
    target=destination/package['directory']
    if target.is_symlink():raise ValueError('Dependency destination must not be a symbolic link')
    with tempfile.TemporaryDirectory(prefix='esp8266-extract-',dir=destination) as temporary:
        temporary=Path(temporary)
        with zipfile.ZipFile(source) as bundle:
            for member in bundle.infolist():
                path=PurePosixPath(member.filename)
                kind=(member.external_attr>>16)&0o170000
                if path.is_absolute()or '..'in path.parts or not path.parts or path.parts[0]!=package['directory']or kind not in(0,stat.S_IFREG,stat.S_IFDIR):
                    raise ValueError('Unexpected member in pinned SDK archive')
                output=temporary.joinpath(*path.parts)
                if member.is_dir():output.mkdir(parents=True,exist_ok=True)
                else:
                    output.parent.mkdir(parents=True,exist_ok=True)
                    with bundle.open(member) as src,output.open('wb') as dst:shutil.copyfileobj(src,dst)
        candidate=temporary/package['directory'];acquirer.verify_identity(candidate,package)
        if target.exists():shutil.rmtree(target)
        os.replace(candidate,target)
    return target

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',required=True,choices=('z-series','picostill','picoferm'))
    parser.add_argument('--destination',type=Path,default=COMMON/'build/dependencies')
    parser.add_argument('--archive-directory',type=Path)
    args=parser.parse_args()
    if platform.system()!='Linux'or platform.machine().lower()not in('x86_64','amd64'):
        raise SystemExit('These pinned Xtensa compilers require Linux x86-64; use that environment or provide separately verified native toolchains.')
    destination=args.destination.resolve();destination.mkdir(parents=True,exist_ok=True)
    cache=(args.archive_directory or destination).resolve();cache.mkdir(parents=True,exist_ok=True)
    acquirer=load_acquirer();results=[]
    # The checked SPIFFS image formatter uses the pinned ESP8266 SPIFFS source
    # for both known ABIs, so its SDK is required in every reference package.
    sdk=package(FERM/'dependencies/arduino-esp8266.json','esp8266-2.4.1','arduino-esp8266')
    source=acquirer.archive(sdk,cache);directory=extract_zip(source,destination,sdk,acquirer)
    results.append(dict(name=sdk['name'],directory=str(directory),archive_sha256=sdk['archive_sha256']))
    if args.model=='picoferm':
        compiler=package(FERM/'dependencies/toolchain.json','xtensa-lx106-elf','xtensa-lx106')
        source=acquirer.archive(compiler,cache);directory=acquirer.extract(source,destination,compiler)
        results.append(dict(name=compiler['name'],directory=str(directory),archive_sha256=compiler['archive_sha256']))
    else:
        for item in json.loads((Z/'dependencies/esp32.json').read_text())['packages']:
            source=acquirer.archive(item,cache);directory=acquirer.extract(source,destination,item)
            results.append(dict(name=item['name'],directory=str(directory),archive_sha256=item['archive_sha256']))
    if args.model=='z-series':
        for name in ('acquire_arduinojson.py','acquire_ethernet2.py'):
            subprocess.run([sys.executable,str(Z/'tools'/name),'--destination',str(destination)],check=True)
    report=dict(schema_version=1,model=args.model,packages=results,hardware_accessed=False,
                script_sha256=acquirer.digest(__file__))
    (destination/(args.model+'-acquisition.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
