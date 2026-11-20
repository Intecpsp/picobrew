#!/usr/bin/env python3
"""Independent original ADC/acquisition/LED vectors; deterministic gzip."""
import gzip,json,random,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def cases():
    rows=[];rng=random.Random(0x027adc)
    def add(op,reads=(),text='',**values):
        w=[0]*24;w[0]=42;w[1]=0x40533333;w[4]=12345;w[5]=923;w[8]=0;w[12]=0xffffffff
        for index,value in values.items():w[int(index[1:])]=value&0xffffffff
        rows.append(dict(id='hardware-%04d'%len(rows),op=op,words=w,reads=list(reads),text=text))
    for _ in range(80):
        reg=rng.randrange(512);value=rng.randrange(512);bit=rng.randrange(64);initial=rng.randrange(256)
        add(0,w2=reg,w3=value);add(1,[initial],w2=reg);add(2,[initial],w2=reg,w3=bit);add(3,[initial],w2=reg,w3=bit)
    for bit in range(8):
        add(4,[0,0,1<<bit],w2=2,w3=bit);add(5,[255,255,255^(1<<bit)],w2=2,w3=bit)
    add(6,[0,4,4,0]);add(7,[0,1,0,0,8,0,0,0,0,0,0,4,0,0,0]);add(8,[0,1,0,8,0,0,0,0,0,0,0,0,0],w2=2,w3=14,w6=42)
    for raw in [0,1,255,65535,0x7fffff,0x800000,0xffffff]+[rng.randrange(1<<24) for _ in range(100)]:
        b=[(raw>>16)&255,(raw>>8)&255,raw&255]
        add(9,b,w2=18);add(10,[0,32]+b+[0]);
    for op,reads in [(11,[255,0,255]),(12,[255]*3),(13,[0,0,255,0])]:add(op,reads)
    for state in [0,1,6,7,12,13,14,0xffffffff]:add(14,w8=state,w9=234,w10=456,w11=789)
    for state in range(13):
        raw=123456 if state==6 else 456789
        add(15,([127] if state==1 else [0] if state==7 else [32,raw>>16,(raw>>8)&255,raw&255,0]),w8=state,w11=98765)
    for raw in [0,0xffffff]:add(15,[32,raw>>16,(raw>>8)&255,raw&255,0],w8=6)
    for raw in [-8388607,-4194304,-1,0,1,4194304,8388607]+[rng.randrange(-8388607,8388608) for _ in range(120)]:
        raw&=0xffffff;add(15,[32,raw>>16,(raw>>8)&255,raw&255,0],w8=12,w11=rng.randrange(34470,234001),w4=rng.randrange(1<<32))
    for raw in [0,1,127,222,512,1023,0x7fffffff,0x80000000,0xffffffff]:add(16,w5=raw)
    for animation in range(10):
        for old in [0xffffffff]+list(range(10)):
            add(17,w2=animation,w12=old,w13=2,w14=5,w15=900,w16=1,w17=1,w18=1,w19=2345,w20=42,w21=2)
    for value in [0,1,2,255,256,257]:
        for mask in range(8):add(18,w2=value,w16=mask&1,w17=(mask>>1)&1,w18=(mask>>2)&1)
    for value in [str(i) for i in range(11)]+['','foo','1 ',' 1','01']:add(19,text=value)
    return rows
if __name__=='__main__':
    output=ROOT/'tests/fixtures/hardware-inputs.json.gz';output.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Generated %d original hardware cases'%len(cases()))
