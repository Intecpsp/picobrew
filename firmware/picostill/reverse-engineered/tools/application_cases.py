#!/usr/bin/env python3
"""Independent original PicoStill application/event inputs."""
import gzip
import json
from pathlib import Path
import controller_cases as ctl
ROOT=Path(__file__).resolve().parents[1]
put=ctl.put
def initial():
    b=bytearray(0x2000);base=ctl.initial();b[0x1a0:0xca0]=base[:0xb00];b[0x1e0:0x504]=bytes(804)
    put(b,0,0x3f412930);put(b,4,6000);put(b,8,20000);put(b,0xf8,0x3ff00078);put(b,0xfc,0x3ff00098)
    put(b,0x5dc,0x3ff001e0);put(b,0x5e0,0x3ff01e00)
    put(b,0x1434,0x3ff00000);put(b,0x15f8,0);put(b,0x1600,1);put(b,0x1614,0x0101a8c0)
    put(b,0x1ef4,3);put(b,0x1ef8,0x3ff0155c);put(b,0x1efc,0)
    put(b,0x1e00,0x3ff01e10);put(b,0x1e10,0x7ff01000)
    for i,pin in enumerate((26,25,33)):put(b,0x155c+i,pin,1)
    put(b,0x15a8,63)
    return b
def cases():
    rows=[]
    def add(op,data=None,arg=0,arg2=0,now=10000):
        rows.append(dict(id='%04d'%len(rows),op=op,data=(initial() if data is None else data).hex(),arg=arg,arg2=arg2,now=now,high=1,pressure=[64,128,0,0],adc=[8,0]*4))
    for pending in (0,1):
        for long in (0,1):
            for count in (0,1,2,0xffffffff):
                b=initial();put(b,0x178,pending,1);add('queue_button',b,long,count)
        for event in (0,1,2,3,0xffffffff):
            for listening in (0,1):
                b=initial();put(b,0x184,pending,1);put(b,0x13c0,listening,1);add('queue_wifi',b,event,3)
    for old in (0,1,7,8,9,13,14):
        for new in tuple(range(15))+(0xffffffff,):
            b=initial();put(b,0xc,old);put(b,0x14,6);put(b,0x188,5);put(b,0x15f8,8);b[0x78:0x7c]=b'net\0';b[0x98:0x9d]=b'pass\0'
            add('state',b,new)
    for state in range(15):
        for long in (0,1):
            for count in (0,1,2,3):
                b=initial();put(b,0xc,state);put(b,0x170,long);put(b,0x174,count);put(b,0x668,1);put(b,0x700,5)
                add('button',b,now=10000)
    for now in (0,5999,6000,6001):
        b=initial();put(b,0x170,1);put(b,0xc,9);add('button',b,now=now)
    for index in (0,1,2,3):
        b=initial();put(b,0xc,8);put(b,0x1efc,index);put(b,0x174,2);add('button',b)
    for state in range(14):
        for event in (0,1,2,3):
            b=initial();put(b,0xc,state);put(b,0x17c,event);put(b,0x180,3);put(b,0x15f8,8);put(b,0x1614,0x01020304)
            add('wifi',b)
    for index in range(3):
        for old in (0,2,3):
            b=initial();put(b,0x1efc,old);add('menu',b,index)
    for program in tuple(range(10))+(0xffffffff,):add('request_program',arg=program)
    add('request_stop')
    for state in range(15):
        for error in (0,6):
            for configured in (0,1):
                b=initial();put(b,0xc,state);put(b,0x14,error);put(b,0x188,5);put(b,0x15f8,8)
                if configured:b[0x78:0x7c]=b'net\0'
                add('poll',b)
    for state in (3,4,5,7,8,9,10,11,12):
        for option in (0,1,2,3):
            b=initial();put(b,0xc,state);put(b,0x15f8,8 if option&1 else 3);put(b,0x100c,option&1,1);put(b,0x100e,(option>>1)&1,1)
            put(b,0x16e8,option*2);put(b,0x16c,option&1,1);put(b,0x16d,(option>>1)&1,1);put(b,0x18c,option&1,1)
            put(b,0x18d,option&1,1);put(b,0x188,5);put(b,0x700,option+1);put(b,0x708,option&1);b[0x78:0x7c]=b'net\0'
            add('poll',b,now=20001)
    for button,wifi in ((1,0),(0,1),(1,1)):
        b=initial();put(b,0xc,7);put(b,0x178,button,1);put(b,0x184,wifi,1);put(b,0x17c,2);put(b,0x174,2);add('poll',b)
    # Actual button helper emits into the application's queue before dispatch.
    for button_state in (1,2):
        b=initial();put(b,0xc,7);put(b,0x1440,button_state);put(b,0x143c,2);put(b,0x1444,0);add('poll',b)
    return rows
if __name__=='__main__':
    path=ROOT/'tests/fixtures/application-inputs.json.gz';path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0))
    print('Wrote %d application/event inputs'%len(cases()))
