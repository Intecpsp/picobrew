"""Package both slot images, their permanent bootstrap and the verified blank filesystem."""
import argparse
import io
from pathlib import Path
import zipfile
import usb_ab
import json

def pack(directory,fs):
    parts={name:(directory/name).read_bytes() for name in usb_ab.NAMES if name!='spiffs.bin'}
    parts['spiffs.bin']=fs
    output=io.BytesIO()
    with zipfile.ZipFile(output,'w') as z:
        for name,data in sorted(parts.items()):
            entry=zipfile.ZipInfo(name,(1980,1,1,0,0,0));entry.create_system=3;entry.external_attr=0o100644<<16
            z.writestr(entry,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
    data=output.getvalue()
    usb_ab.parse(data,json.loads(parts['ota-manifest.json'])['version'])
    return data

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('build',type=Path);p.add_argument('spiffs',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args()
    data=pack(a.build,a.spiffs.read_bytes())
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_bytes(data)
