#!/usr/bin/env python3
"""Controller inputs in the original object's memory layout, not source output."""
import gzip
import json
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[1]
SIZE=0xe24
def f(value):return struct.unpack('<I',struct.pack('<f',value))[0]
def put(data,offset,value,size=4):data[offset:offset+size]=int(value).to_bytes(size,'little',signed=False)
def initial():
    b=bytearray(SIZE)
    put(b,0x368,0x3f4128f4);put(b,0x3d8,18,1);put(b,0x420,19,1)
    for offset,channel,pin,tach in ((0x3e0,1,12,5),(0x400,2,2,17)):
        put(b,offset,channel,1);put(b,offset+1,pin,1);put(b,offset+2,tach,1)
    s=0x428
    for offset,value in ((0,2000),(4,f(-1)),(8,f(35)),(12,f(2.8)),(16,f(10)),(20,0x3ff00b00),(24,0x3ff01e00)):
        put(b,s+offset,value)
    for offset,value in ((0x1c,0x34),(0x38,0x40),(0x39,0x18),(0x3a,25)):put(b,s+offset,value,1)
    for i in range(5):put(b,s+0x58+4*i,f(20 if i<4 else 14));put(b,s+0x74+4*i,f(1));put(b,s+0x88+4*i,f(20 if i<4 else 14))
    put(b,s+0x6c,2);put(b,s+0x70,2);put(b,0xaa8,120000);put(b,0xaac,20000)
    return b
def cases():
    rows=[]
    def add(op,data=None,arg=0,now=1000,high=0,pressure=None,adc=None):
        rows.append(dict(id='%04d'%len(rows),op=op,data=(initial() if data is None else data).hex(),arg=arg,now=now,high=high,
                         pressure=[64,128,0,0] if pressure is None else pressure,adc=[8,0]*4 if adc is None else adc))
    for enabled in (0,1):
        for recorded in (0,1):
            for new in (0,1):
                for op in ('enable','enable_internal','enable_record'):
                    b=initial();put(b,0x47c,enabled,1);put(b,0x47d,recorded,1);put(b,0x478,9123);put(b,0xe20,2)
                    put(b,0xb00,f(4));put(b,0xb04,f(-2));add(op,b,new)
    for sample in (0,1,2,60):
        b=initial();put(b,0x494,sample);put(b,0x49c,f(.5));put(b,0x480,f(-2));put(b,0x3f2,255,1)
        put(b,0x3f4,17);put(b,0x414,33);put(b,0x3f8,f(1900));put(b,0x418,f(2100))
        for off in (0x370,0x670,0x6e0):
            for i in range(24):put(b,off+i*4,0x3f000000+i)
        add('capture',b)
    for enabled in (0,1):
        for sensor_ready in (0,1):
            for sensor_good in (0,1):
                for in_range in (0,1):
                    for fan in (0,1):
                        b=initial();put(b,0x47c,enabled,1);put(b,0x494,2 if sensor_ready else 0);put(b,0x498,2 if sensor_ready else 0)
                        put(b,0x49c,f(1 if sensor_good else .5));put(b,0x4b0,f(20 if in_range else 81))
                        put(b,0x3f1,fan,1);put(b,0x3f2,255,1);put(b,0x3f4,5);put(b,0x3f8,f(1000));put(b,0x3d0,19)
                        add('hardware_error',b)
    for temperatures in ((20,20,20,20),(10,30,20,20),(9.999,20,20,20),(20,30.001,20,20),(20,20,float('nan'),20)):
        for elapsed in (0,59999,60000,0xffffffff,0x100000000):
            for passed in (0,1):
                b=initial();put(b,0x6d8,1,1);put(b,0x6da,passed,1);put(b,0x6d0,1000,8)
                for i,v in enumerate(temperatures):put(b,0x480+i*4,f(v))
                add('pin_test',b,1000+elapsed)
    for runtime in (0,4999,5000,5001,0x100000000,0xffffffffffffffff):
        for ready in (0,1):
            for good in (0,1):
                b=initial();put(b,0x668,1,1);put(b,0x3f4,5 if ready else 0);put(b,0x414,5 if ready else 0)
                put(b,0x4b0,f(20 if good else 81));add('connection_test',b,runtime)
    for op in ('pin_test','connection_test'):
        for started,done in ((0,0),(0,1),(1,1)):
            b=initial();off=0x6d0 if op=='pin_test' else 0x660;put(b,off+8,started,1);put(b,off+9,done,1);add(op,b,200000)
    for old in (0,1,2,3,4,5,6,7,8,9,0xffffffff):
        for new in (0,1,2,3,4,5,6,7,8,9,0xffffffff):
            b=initial();put(b,0x4c8,old);put(b,0x47c,old&1,1);put(b,0x47d,(old>>1)&1,1)
            put(b,0x6da,1,1);put(b,0x578,0x123456789,8);put(b,0x580,9999,8);put(b,0x588,1234567,8)
            put(b,0x5fc,10);put(b,0x66c,12);put(b,0x740,13);put(b,0x421,1,1)
            add('state',b,new,now=0xfffffff0 if new==2 else 1000)
    for old in (0,1,8):
        for program in tuple(range(11))+(0xffffffff,):
            b=initial();put(b,0x4c8,old);put(b,0x568,17);add('start',b,program)
    for program in (0,1,2,3,4,5,6,8,9,0xffffffff):
        for state in (0,1,8):
            b=initial();put(b,0x4c8,state);put(b,0x560,program);put(b,0x564,0xfffffff0);put(b,0x568,5)
            put(b,0x5fc,10);put(b,0x66c,12);put(b,0x740,13);add('stop',b,now=100)
    for pump in (0,1,9,10,11,20):
        for connection in (0,12,13):
            for pin in (0,13):
                b=initial();put(b,0x4c8,8);put(b,0x560,4);put(b,0x568,5);put(b,0x5fc,pump);put(b,0x66c,connection);put(b,0x740,pin)
                for off in (0x600,0x670):
                    for i in range(24):put(b,off+4*i,f((off-0x600)/16+i))
                add('stop',b)
    for state in range(10):
        for temp in (31,32.2,35,36,float('nan')):
            for elapsed in (19999,20000,120001):
                b=initial();put(b,0x4c8,state);put(b,0x560,6 if state<4 else (5 if state==4 else 4));put(b,0x484,f(temp));put(b,0x56c,120000)
                # Sensor updates disabled so controller thresholds use the supplied sample.
                put(b,0x6d9,1,1);put(b,0x669,1,1);put(b,0x570,5);add('poll',b,now=elapsed)
    for enabled in (0,1):
        for record in (0,1):
            for count in (0,199,200,201):
                for due in (0,1):
                    b=initial();put(b,0x47c,enabled,1);put(b,0x47d,record,1);put(b,0xe20,count);put(b,0x478,0 if due else 2000)
                    add('hardware_poll',b,pressure=[64,128,0,0] if count&1 else [],adc=[8,0]*4 if due else [])
    # Pump-on enforces fan 2 power. Fans sample independently before error decisions.
    for error in (0,6,7):
        b=initial();put(b,0x4c8,1);put(b,0x560,6);put(b,0x421,1,1);put(b,0x3d0,error);add('poll',b)
    for both in (0,1):
        b=initial();put(b,0x3f1,1,1);put(b,0x411,both,1);put(b,0x3f2,255,1);put(b,0x412,255,1);put(b,0x3f4,5);put(b,0x414,5)
        put(b,0x3fc,4);put(b,0x41c,4);add('hardware_poll',b,now=50,high=1)
    for power in (0,1):
        for first in (0,1):
            for second in (0,1):
                for pump in (0,1):
                    b=initial();put(b,0x3d9,power,1);put(b,0x3f1,first,1);put(b,0x411,second,1);put(b,0x421,pump,1)
                    add('flags',b)
    for state in (5,6,7,8):
        for phase in (0,1,4,5):
            for elapsed in (1000,60001,400001):
                b=initial();put(b,0x4c8,state);put(b,0x560,state-4 if state<8 else 4);put(b,0x570,phase)
                put(b,0x588,60000,8);put(b,0x5f0,f(14));put(b,0x3f4,5);put(b,0x414,5);put(b,0x3f8,f(1800));put(b,0x418,f(1800))
                put(b,0x6d9,phase==5,1);put(b,0x668,phase>0,1);add('poll',b,now=elapsed)
    return rows
if __name__=='__main__':
    path=ROOT/'tests/fixtures/controller-inputs.json.gz';path.parent.mkdir(exist_ok=True)
    path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote %d controller inputs'%len(cases()))
