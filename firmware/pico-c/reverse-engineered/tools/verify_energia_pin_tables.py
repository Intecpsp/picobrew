#!/usr/bin/env python3
"""Verify complete Energia digital-pin tables in both hash-checked C images."""
import argparse
import hashlib
import json
from pathlib import Path
import os
import shlex
import struct
import subprocess
import tempfile
from inspect_oem import VARIANTS, read_variant

TABLES = ('port_to_base', 'digital_pin_to_pin_num', 'digital_pin_to_port',
          'digital_pin_to_bit_mask', 'digital_pin_to_timer')
REQUIRED = TABLES[:4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    source = '#define ARDUINO_MAIN\n#include <Energia.h>\n#include <stdio.h>\nint main(void) {\n'
    for table in TABLES:
        source += ('unsigned n_' + table + '=sizeof(' + table + '); fwrite(&n_' + table +
                   ',4,1,stdout);fwrite(' + table + ',1,sizeof(' + table + '),stdout);\n')
    source += 'return 0;}\n'
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    with tempfile.TemporaryDirectory(prefix='pico-c-pin-tables-') as temp:
        root = Path(temp)
        (root/'probe.c').write_text(source)
        executable = root / ('probe.exe' if os.name == 'nt' else 'probe')
        subprocess.run([*compiler, '-std=gnu11', '-DF_CPU=80000000L',
            '-I'+str(sdk/'cores/cc3200'), '-I'+str(sdk/'system'),
            '-I'+str(sdk/'variants/CC3200-LAUNCHXL'), str(root/'probe.c'), '-o', str(executable)],
            check=True, capture_output=True)
        payload = subprocess.check_output([str(executable)])
    tables, offset = {}, 0
    for name in TABLES:
        length = struct.unpack_from('<I',payload,offset)[0]; offset += 4
        if not 0 < length <= 1024:
            raise ValueError('Unexpected native table representation')
        tables[name] = payload[offset:offset+length]; offset += length
    records=[]
    for variant in VARIANTS:
        image=read_variant(variant); matches={}
        for name,data in tables.items():
            locations=[];offset=0
            while (offset:=image.find(data,offset)) >= 0:
                locations.append(hex(0x20004000+offset));offset+=1
            matches[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'locations':locations}
        passed=all(len(matches[name]['locations'])==1 for name in REQUIRED)
        records.append({'variant':variant,'image_sha256':VARIANTS[variant]['raw_sha256'],
                        'passed':passed,'tables':matches})
    report={'passed':all(r['passed'] for r in records),'sdk_variant':'CC3200-LAUNCHXL','variants':records,
            'scope':'Exact complete digital pin-number, port-index, bit-mask and port-base array matches.',
            'limits':'No PWM/timer-table match is required; matching these arrays does not establish whole-SDK or physical hardware equivalence.'}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
