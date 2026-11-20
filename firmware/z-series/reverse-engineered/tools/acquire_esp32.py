#!/usr/bin/env python3
"""Acquire the hash-pinned historical ESP32 compiler and Arduino SDK reference."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile
import urllib.request
ROOT=Path(__file__).resolve().parents[1]
PIN=ROOT/'dependencies/esp32.json'

def digest(path):
    result=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):result.update(block)
    return result.hexdigest()

def archive(package,cache):
    result=cache/package['archive']
    if not result.exists():
        with tempfile.NamedTemporaryFile(prefix=package['name']+'-',suffix='.download',dir=cache,delete=False) as output:
            temporary=Path(output.name)
            try:
                with urllib.request.urlopen(package['url'],timeout=60) as response:
                    size=0
                    while True:
                        block=response.read(1024*1024)
                        if not block:break
                        size+=len(block)
                        if size>package['archive_bytes']:raise ValueError('Archive exceeds pinned size')
                        output.write(block)
                output.flush()
                if size!=package['archive_bytes'] or digest(temporary)!=package['archive_sha256']:
                    raise ValueError('Downloaded archive differs from pinned bytes')
                os.replace(temporary,result)
            finally:
                if temporary.exists():temporary.unlink()
    if result.stat().st_size!=package['archive_bytes'] or digest(result)!=package['archive_sha256']:
        raise ValueError('Cached archive differs from pinned bytes: '+str(result))
    return result

def verify_identity(directory,package):
    for name,expected in package['identity_sha256'].items():
        if digest(directory/name)!=expected:raise ValueError('Extracted dependency identity differs: '+name)

def extract(source,destination,package):
    directory=destination/package['directory']
    if directory.is_symlink():raise ValueError('Dependency destination must not be a symbolic link')
    with tempfile.TemporaryDirectory(prefix='esp32-extract-',dir=destination) as temporary:
        temporary=Path(temporary)
        with tarfile.open(source,'r:gz') as bundle:
            for member in bundle.getmembers():
                name=PurePosixPath(member.name)
                if name.is_absolute() or '..' in name.parts or not name.parts or name.parts[0]!=package['directory']:
                    raise ValueError('Archive member is outside the pinned root')
                if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                    raise ValueError('Unexpected special archive member')
            # The standard data filter additionally rejects escaping link targets
            # and unsafe modes. The official compiler archive contains links.
            bundle.extractall(temporary,filter='data')
        candidate=temporary/package['directory']
        verify_identity(candidate,package)
        # This directory is a generated, commit-ignored dependency cache. A
        # fresh extraction prevents modified cached headers from passing merely
        # because a few identity files are unchanged.
        if directory.exists():shutil.rmtree(directory)
        os.replace(candidate,directory)
    return directory

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,default=ROOT/'build/dependencies')
    parser.add_argument('--archive-directory',type=Path,help='Optional cache of the exact pinned download archives')
    args=parser.parse_args();destination=args.destination.resolve();destination.mkdir(parents=True,exist_ok=True)
    cache=(args.archive_directory or destination).resolve();cache.mkdir(parents=True,exist_ok=True)
    pin=json.loads(PIN.read_text());results=[]
    for package in pin['packages']:
        source=archive(package,cache);directory=extract(source,destination,package)
        results.append(dict(name=package['name'],directory=str(directory),archive_sha256=package['archive_sha256']))
    report=dict(schema_version=1,pin_sha256=digest(PIN),packages=results,complete_application_built=False,hardware_accessed=False)
    (destination/'esp32-acquisition.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(results,indent=2))
    return 0
if __name__=='__main__':raise SystemExit(main())
