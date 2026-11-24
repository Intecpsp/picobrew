"""Validate a PicoFerm A/B package and compose the first, backup-protected USB install."""
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import sys
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'firmware/common/tools'))
import esp_provisioning
BANKS=(0x3000,0x83000)
BANK_SIZE=0x7d000
BOOTSTRAP_SHA256='d6e1a05f85732b1bcc94400c8c0f5fb505bb9c5e5d69820a93145620705eb2de'
FS=slice(0x100000,0x1fb000)
NAMES={'ota-manifest.json','bootstrap.bin','bank-0.bin','bank-1.bin','spiffs.bin'}
LIMITS={'ota-manifest.json':4096,'bootstrap.bin':4096,'bank-0.bin':BANK_SIZE,
        'bank-1.bin':BANK_SIZE,'spiffs.bin':FS.stop-FS.start}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def _decode(data, count):
    if len(data)<16 or len(data)%16 or data[:4]!=bytes((0xe9,count,2,0x30)):
        raise ValueError('Unexpected ESP8266 image header')
    (entry,)=struct.unpack_from('<I',data,4)
    offset=8; checksum=0xef; segments=[]
    for _ in range(count):
        if offset+8>len(data):raise ValueError('Truncated image')
        address,size=struct.unpack_from('<II',data,offset);offset+=8
        if not size or size%4 or address%4 or size>len(data)-offset:
            raise ValueError('Invalid segment length or address')
        segments.append((address,size))
        for b in memoryview(data)[offset:offset+size]:checksum^=b
        offset+=size
    if len(data)!=(offset|15)+1 or any(data[offset:-1]) or data[-1]!=checksum:
        raise ValueError('Invalid image checksum')
    return entry,segments

def parse(archive,version):
    if len(archive)>2*1024*1024 or not re.fullmatch(r'[0-9]{1,5}(\.[0-9]{1,5}){2}',version):
        raise ValueError('Invalid PicoFerm package or version')
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        if set(z.namelist())!=NAMES or len(z.infolist())!=len(NAMES):
            raise ValueError('Incomplete or ambiguous PicoFerm A/B package')
        if any(i.file_size<1 or i.file_size>LIMITS[i.filename] for i in z.infolist()):
            raise ValueError('PicoFerm package component exceeds its flash region')
        parts={name:z.read(name) for name in NAMES}
    meta=json.loads(parts.pop('ota-manifest.json'))
    if (meta.get('schema_version'),meta.get('protocol'),meta.get('model'),meta.get('version'))!=(
        1,'esp8266-ab-v1','picoferm',version):
        raise ValueError('Package targets another firmware or board')
    boot=parts['bootstrap.bin']
    pc,segments=_decode(boot,1)
    start,size=segments[0]
    if start!=0x4010f000 or start+size>0x40110000 or not start<=pc<start+size or sha(boot)!=BOOTSTRAP_SHA256 or meta['bootstrap_sha256']!=BOOTSTRAP_SHA256:
        raise ValueError('Invalid PicoFerm bootloader')
    if len(meta['images'])!=2:raise ValueError('Expected both PicoFerm banks')
    for bank,base in enumerate(BANKS):
        name=f'bank-{bank}.bin';data=parts[name]
        expected=dict(bank=bank,offset=base,size=len(data),file=name,sha256=sha(data))
        if meta['images'][bank]!=expected:raise ValueError('Slot identity or digest differs')
        pc,segments=_decode(data,4)
        (mapped,_),(iram,length),(dram,dlen),(rodata,rlen)=segments
        if (mapped!=0x40200010+base or iram!=0x40100000 or not iram<=pc<iram+length<=0x40108000
                or not 0x3ffe8000<=dram<dram+dlen<=rodata<rodata+rlen<=0x3fff8000 or pc%4):
            raise ValueError('Incorrect slot link or memory map')
        tag=b'PicoFerm-AB1:'+version.encode()
        if data[16:48]!=tag.ljust(32,b'\0'):raise ValueError('Image model or version differs')
    if sha(parts['spiffs.bin'])!=esp_provisioning.EMPTY['esp8266-2m']:
        raise ValueError('Package does not contain the verified empty PicoFerm filesystem')
    return parts

def target(flash,original_length,parts,api_key):
    if (len(flash)!=0x200000 or not 16<=original_length<=FS.start
            or flash[:4]!=parts['bootstrap.bin'][:4] or flash[original_length:FS.start].strip(b'\xff')):
        raise ValueError('Unknown data or geometry in the future PicoFerm OTA banks')
    if flash[FS].strip(b'\xff') and flash[FS]!=parts['spiffs.bin']:
        raise ValueError('Existing flash data overlaps the proposed PicoFerm filesystem')
    app=parts['bank-0.bin']
    body=struct.pack('<IIIII',0x31424650,0,1,len(app),zlib.crc32(app))
    record=body+struct.pack('<II',zlib.crc32(body),0xfffffff8)
    out=bytearray(flash)
    out[:FS.start]=b'\xff'*FS.start
    out[:len(parts['bootstrap.bin'])]=parts['bootstrap.bin']
    out[0x1000:0x1000+len(record)]=record
    out[BANKS[0]:BANKS[0]+len(app)]=app
    out[FS]=esp_provisioning.stage(parts['spiffs.bin'],'esp8266-2m',api_key) if api_key else parts['spiffs.bin']
    return bytes(out)


def matches_key(fs, key):
    """Read the active small key object using the SDK's ESP8266 SPIFFS geometry."""
    if len(fs)!=0xfb000 or not isinstance(key,str) or not re.fullmatch(r'[0-9a-f]{64}',key):
        return False
    page_size,per_block=256,32
    found={}
    for block in range(len(fs)//(page_size*per_block)):
        base=block*page_size*per_block
        if struct.unpack_from('<H',fs,base+252)[0] != (0x529^256):
            return False
        for entry in range(per_block-1):
            obj=struct.unpack_from('<H',fs,base+entry*2)[0]
            if obj in (0,0xffff) or not obj&0x8000:continue
            header=base+(entry+1)*page_size
            if struct.unpack_from('<HHB',fs,header)!=(obj,0,0xf8):continue
            name=bytes(fs[header+13:header+45]).split(b'\0',1)[0]
            if name not in (b'/pb/api.key',b'/pb/api.key.bak'):continue
            length=struct.unpack_from('<I',fs,header+8)[0]
            page=struct.unpack_from('<H',fs,header+46)[0]
            if (name in found or length not in (64,65,66) or fs[header+12]!=1
                    or page>=len(fs)//page_size or page%per_block==0):
                return False
            start=page*page_size
            lookup=(page//per_block)*page_size*per_block+2*(page%per_block-1)
            if (struct.unpack_from('<HHB',fs,start)!=(obj&0x7fff,0,0xfc)
                    or struct.unpack_from('<H',fs,lookup)[0]!=(obj&0x7fff)):
                return False
            found[name]=fs[start+5:start+5+length]
    expected=key.encode()
    return found.get(b'/pb/api.key',found.get(b'/pb/api.key.bak')) in (expected,expected+b'\n',expected+b'\r\n')


def migrate(current, original, wanted, key, releases):
    """Retain all live private storage and SDK data during a recognized single-bank migration."""
    if len(current)!=0x200000 or len(original)!=len(current) or len(wanted)!=len(current):
        raise ValueError('PicoFerm flash geometry changed')
    if not matches_key(current[FS],key):
        raise ValueError("The PicoFerm key differs from the Hub's saved pairing")
    if current[:FS.start]==wanted[:FS.start]:
        return current
    allowed=[r for r in releases if r.get('model')=='picoferm' and r.get('kind')=='reference'
             and isinstance(r.get('binary_bytes'),int) and 0<r['binary_bytes']<=FS.start]
    for entry in allowed:
        end=entry['binary_bytes']
        if sha(current[:end])!=entry.get('binary_sha256'):
            continue
        if current[end:FS.start]!=original[end:FS.start]:
            raise ValueError('Unexplained bytes occupy a future PicoFerm OTA bank')
        return wanted[:FS.start]+current[FS.start:]
    raise ValueError('The installed PicoFerm application differs from every published single-bank image')
