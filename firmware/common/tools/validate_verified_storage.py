#!/usr/bin/env python3
"""Run shared verified-storage fault injection and record exact source evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
FILES=['include/picobrew/verified_storage.hpp','src/verified_storage.cpp',
       'tests/verified_storage_test.cpp','tests/test_verified_storage.py',
       'tools/validate_verified_storage.py']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-report',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='picobrew-storage-evidence-') as directory:
        executable=Path(directory)/'verified-storage'
        flags=['-std=c++11','-O2','-Wall','-Wextra','-Werror']
        subprocess.run(['g++']+flags+['-I'+str(ROOT/'include'),str(ROOT/'src/verified_storage.cpp'),
                                    str(ROOT/'tests/verified_storage_test.cpp'),'-o',str(executable)],check=True)
        results=json.loads(subprocess.check_output([str(executable)],text=True))
    report=dict(schema_version=1,compiler=subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
        flags=flags,results=results,source_sha256={name:digest(ROOT/name) for name in FILES},
        boundary='Actual shared production transaction executes against a deterministic FileIo filesystem model. Every callback in replacement, first creation and removal is failed once; persisted snapshots before/after mutation model reset/remount. Partial transfer, readback corruption, wrong length, zero progress, invalid keys and main I/O versus absent-main fallback are checked. Actual pinned SPIFFS driver validation is separate.',
        hardware_tested=False,single_rename_atomicity_claimed=False)
    if args.write_report:
        output=ROOT/'analysis/verified-storage-validation.json';output.parent.mkdir(exist_ok=True)
        output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
