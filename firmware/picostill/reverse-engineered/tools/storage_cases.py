#!/usr/bin/env python3
"""Bounded NVS resize/read/write cases; allocation/SDK effects are explicit."""
import gzip
import json
from pathlib import Path
def cases():
    out=[]
    def add(op='begin',requested=512,blob=100,size=0,present=0,dirty=0,version=0,opened=0,query=0,read=0,write=0,expand=0,allocation=0):
        out.append(dict(id='storage-%04d'%len(out),op=op,requested=requested,blob=blob,size=size,present=present,dirty=dirty,version=version,
                        opened=opened,query=query,read=read,write=write,expand=expand,allocation=allocation))
    for op in ('begin','initialize'):
        for requested in (0,1,99,100,101,512,1024):
            for blob in (-1,0,1,100,512,1024):add(op,requested,blob)
        for blob in (-1,100,512,1024):
            for field,value in (('opened',0x1105),('query',0x1107),('read',0x1108),('write',0x1105),('expand',0x1105),('allocation',1),('allocation',2),('allocation',3)):
                add(op,512,blob,**{field:value})
        for requested,blob in ((100,512),(512,100),(512,512)):
            add(op,requested,blob,100,1,1)
    for op in ('load','save'):
        for size in (0,1,99,100,101,512):
            for dirty in (0,1):
                for version in (0,1,0xffffffff):add(op,512,100,size,1,dirty,version)
        for size in (0,1,99,100,512):add(op,512,100,size,1,1,3,write=0x1105)
    return out
if __name__=='__main__':
    path=Path(__file__).resolve().parents[1]/'tests/fixtures/storage-inputs.json.gz'
    path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0))
    print('Wrote',len(cases()),'storage cases')
