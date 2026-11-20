#!/usr/bin/env python3
"""Original Wi-Fi state, timeout, network-result and status-label domains."""
import gzip
import json
import struct
from pathlib import Path
def cases():
    out=[]
    def add(op='poll',arg=0,state=0,requested=8,now=20000,changed=1000,internet=0,network=None,empty_ap=False):
        data=bytearray(0x4c)
        for offset,value in ((0,500),(4,1000),(8,10000),(12,0x3ff00800),(16,0x3ff00300),(20,internet),(24,0x3ff00380),(28,0x3ff00340),
                             (32,state),(36,changed),(40,requested),(44,0x3ff003c0),(48,0x3f413a90),(52,0x0104a8c0),
                             (56,0x3f413a90),(60,0x0a090807),(64,0x3f413a90),(68,0x01010101),(72,0)):
            struct.pack_into('<I',data,offset,value)
        out.append(dict(id='wifi-%04d'%len(out),op=op,arg=arg,data=data.hex(),now=now,empty_ap=empty_ap,
                        network=network or [3,2,0,1,1,0x0104a8c0,0x0a01a8c0,0x0101a8c0,1,0x08080808,1,1]))
    for old in range(12):
        for new in range(12):add('state',new,old)
    for state in range(12):
        for requested in (1,3,8,10):
            for now in (1499,1500,1501,1999,2000,2001,10999,11000,11001):add(state=state,requested=requested,now=now)
    for state in (4,5,6,9):
        for changed in (0xfffffff0,0x80000000):
            for now in (0,499,1000,9999,10000,0xfffffff0):add(state=state,now=now,changed=changed)
    for status in (0,1,2,3,4,5,6,7,255,259):
        for state in (6,8):
            net=[status,2,0,1,1,0x0104a8c0,0x0a01a8c0,0x0101a8c0,1,0x08080808,1,1]
            for now in (11000,11001):add(state=state,now=now,network=net)
    for scan in (-2,-1,0,1,10):
        for requested in (1,3,8,10):
            net=[3,scan,0,1,1,0x0104a8c0,0x0a01a8c0,0x0101a8c0,1,0x08080808,1,1];add(state=2,requested=requested,network=net)
    for internet in (0,1):add(state=7,internet=internet)
    for index,values in ((2,(0,1,3,6,255)),(3,(0,1)),(4,(0,1,8)),(5,(0,0x0104a8c0)),(6,(0,0xffffffff)),(8,(0,1,2)),(10,(0,1)),(11,(0,1))):
        for value in values:
            net=[3,2,0,1,1,0x0104a8c0,0x0a01a8c0,0x0101a8c0,1,0x08080808,1,1];net[index]=value
            if index==2:add('state',6,network=net)
            elif index in (3,4,5):add('state',3,network=net)
            elif index in (6,8):add('state',7,network=net)
            else:add(state=4 if index==10 else 9,network=net)
    add('state',3,empty_ap=True)
    for state in range(10):
        for error in (0,2,3,4,5):add('fail',error,state)
    for status in range(256):add('label',status)
    return out
if __name__=='__main__':
    path=Path(__file__).resolve().parents[1]/'tests/fixtures/wifi-inputs.json.gz';path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote',len(cases()),'Wi-Fi cases')
