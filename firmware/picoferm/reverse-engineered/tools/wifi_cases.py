#!/usr/bin/env python3
import gzip,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def cases():
    rows=[]
    def add(op,arg=0,error='',empty=False,**values):
        w=[2000,0,1,0,0,0,0,0x0104a8c0,0,12345,3,2,1,0,1,0x12345678,0x0201a8c0,0x0101a8c0]
        for key,value in values.items():w[int(key[1:])]=value&0xffffffff
        rows.append(dict(id='wifi-%04d'%len(rows),op=op,arg=arg,words=w,error=error,empty=empty))
    for old in range(10):
        for new in range(10):add('state',new,w1=old)
    for begin in [0,1,2,3,4,5,6,255,256,0xffffffff]:add('state',4,w13=begin)
    add('state',3,w12=0);add('state',5,w14=0);add('state',5,w14=2);add('state',6,w16=0,w17=0xffffffff)
    for state in range(10):
        for target in [1,3,6]:
            for status in [0,1,2,3,4,5,6,255,256]:
                for ready in [0,1]:add('poll',w1=state,w2=target,w10=status,w4=ready)
    for status in [0,1,2,3,4,5,6,255,256]:add('poll',w1=4,w2=6,w5=1,w6=status)
    for complete in [0,1,127,128,255,256,0xffffffff,0xfffffffe]:add('poll',w1=2,w11=complete)
    for stamp,timeout,now in [(0,2000,1999),(0,2000,2000),(0,2000,2001),(0xffffff00,2000,0xffffffff),(0xffffff00,2000,0),(0xffffff00,2000,1744),(0xffffff00,2000,1745)]:add('poll',w1=7,w0=timeout,w3=stamp,w9=now)
    for status in [0,1,2,3,4,5,6,255,256]:
        for target in [1,3,6]:
            for error in ['', 'already failed']:add('fail',status,error=error,w1=4,w2=target)
    for arg in list(range(10))+[255,256,0xffffffff]:add('request',arg);add('label',arg)
    for status in list(range(10))+[255,256,0xffffffff]:add('status',status)
    add('initialize');add('initialize',empty=True);add('connect');add('connect',empty=True)
    return rows
if __name__=='__main__':
    (ROOT/'tests/fixtures/wifi-inputs.json.gz').write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Generated %d Wi-Fi cases'%len(cases()))
