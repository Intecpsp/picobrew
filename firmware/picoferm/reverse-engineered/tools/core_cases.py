#!/usr/bin/env python3
"""Independent inputs for original PicoFerm instruction execution."""
import gzip
import json
from pathlib import Path
import random
import struct

ROOT=Path(__file__).resolve().parents[1]
def f(value): return struct.unpack('<I',struct.pack('<f',value))[0]
def word(data,at,value): struct.pack_into('<I',data,at,value & 0xffffffff)
def record(**fields):
    data=bytearray((i*37+11)&255 for i in range(236))
    word(data,0x65,100000);word(data,0x69,0xdeadbeef);data[0x6d]=1;word(data,0x6e,110840);word(data,0x72,1);data[0x76]=0;data[0x8b]=0
    offsets={'offset':0x65,'cookie':0x69,'slope':0x6e,'version':0x72}
    byte_offsets={'has_offset':0x6d,'start':0x76,'count':0x8b}
    for key,value in fields.items():
        if key in offsets:word(data,offsets[key],value)
        else:data[byte_offsets[key]]=value
    return data

def cases():
    rows=[]
    def add(op,**kw): rows.append(dict(id='%04d'%len(rows),op=op,**{k:(v.hex() if isinstance(v,(bytes,bytearray)) else v) for k,v in kw.items()}))
    add('assertion');add('warning')
    responses=['','2','10','0','1','3','11','258','266','2,','10,','2,0','10,1','2,2147483647','10,2147483648',
               '2,4294967295','2,999999999999999999999999','99999999999999999,2','0002,00013',',',',2',',,','2,,1','2,10,7','10,2,9',
               '2,7x','2,x','2x','x2',' 2','-2','+2','2,-1','2,+1','2, 1','2\n','2,3\n','10,1,','2\0,99','2,3\0,4']
    rng=random.Random(20261120)
    responses += [''.join(rng.choice('012,xa-') for _ in range(rng.randrange(1,24))) for _ in range(80)]
    for response in responses:add('session',response=response)
    for data in (bytes(236),bytes([255])*236,record()):add('reset',data=data)
    for count in (0,1,3,4,5,12,255):
        for bits in (0,0x80000000,f(.009),f(.01),f(.010001),f(-.01),f(-.010001),f(1),0x7f800000,0xff800000,0x7fc00000):
            data=record(count=count,start=11,version=0,cookie=0);data[0x8c:0xac]=bytes(32)
            word(data,0x8c+(len(rows)%8)*4,bits);add('migrate',data=data)
    for slope in (0,89999,90000,110840,150000,150001,0xffffffff):
        for offset in (0,34469,34470,34471,233999,234000,234001,0xffffffff):
            for has_offset in (0,1,255):add('calibration',data=record(slope=slope,offset=offset,has_offset=has_offset))
    add('sanitize',data=bytes([255])*236);add('sanitize',data=bytes(236))
    for cookie,version in ((0xdeadbeef,1),(0xdeadbeef,2),(0xdeadbeef,0),(0,0),(0,1)):
        for slope in (0,89999,90000,150000,150001,0xffffffff):
            for offset in (0,100000,234001):add('sanitize',data=record(cookie=cookie,version=version,slope=slope,offset=offset))
    for start in (0,1,11,12,127,255):
        for count in (0,1,11,12,13,24,255):
            add('append',data=record(start=start,count=count),temperature=f(21.375),pressure=f(4.125))
    for count in (0,1,11,12,13,255):
        for processed in (0,1,10,11,12,0xffffffff):
            for index in (0,11,0xffffffff):add('iterator',data=record(count=count),processed=processed,index=index)
    for gpio in (0,1,127,128,255,256,0xffffffff):add('button',gpio=gpio)
    for state in (0,1,2,3,0xffffffff):
        for gpio in (0,1,128,255):add('persist',data=record(),machine_state=0,session_state=state,gpio=gpio)
    for machine in (0,4,8,0xffffffff):
        for slope in (0,1,89999,110840,150001,0xffffffff):
            for raw in (0,99999,100000,100001,234000,0xffffffff):
                add('adc',data=record(slope=slope),machine_state=machine,session_state=2,gpio=1,temperature=f(22.25),raw_pressure=raw)
    for has_offset,offset in ((0,100000),(1,0),(255,234000)):
        add('adc',data=record(has_offset=has_offset,offset=offset),machine_state=4,session_state=1,gpio=0,temperature=f(18.5),raw_pressure=123456)
    return rows

if __name__=='__main__':
    path=ROOT/'tests/fixtures/core-inputs.json.gz';path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0))
    print('Wrote %d deterministic original-execution inputs'%len(cases()))
