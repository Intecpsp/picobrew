#!/usr/bin/env python3
import gzip,json,random,struct
from pathlib import Path
from core_cases import record,word,f
ROOT=Path(__file__).resolve().parents[1]
def stored(count=0,start=0):
    data=record(count=count,start=start);data[:96]=bytes(96);data[:5]=b'Home\0';data[32:37]=b'Pass\0'
    for i in range(12):word(data,0x8c+i*8,f(60+i*.25));word(data,0x90+i*8,f(i*.1))
    return data
def cases():
    rows=[];rng=random.Random(0x2fda)
    def add(op,first='',second='',raw=b'',capacity=128,data=None,record_data=None):
        rows.append(dict(id='data-%04d'%len(rows),op=op,first=first,second=second,raw=raw.hex(),capacity=capacity,data=(data or bytes([0xa5])*112).hex(),record=(record_data or stored()).hex()))
    for capacity in [1,2,8,16,32,64,128]:
        for length in [0,1,capacity-1,capacity,capacity+1,200]:add(0,first='a'*length,capacity=capacity)
    for length in list(range(16))+[31,63]:add(1,raw=bytes(rng.randrange(256) for _ in range(length)))
    for version in ['','0.2.7','a'*15,'b'*16,'c'*63]:
        for website in ['','www.picobrew.com','z'*31,'x'*32,'q'*64]:add(2,first=version,second=website)
    for length in range(8):add(3,raw=bytes(rng.randrange(256) for _ in range(length)))
    for first,second in [('', ''),('abcd','ABCD'),('abcde','ABCDE'),('abcdabcd','abcdabcd')]+[(''.join(rng.choice('abcdef0123') for _ in range(rng.randrange(70))),''.join(rng.choice('abCD012345') for _ in range(rng.randrange(100)))) for _ in range(110)]:add(4,first=first,second=second)
    for first in ['', 'Home', 's'*31,'s'*32,'s'*80]:
        for second in ['', 'Pass', 'p'*63,'p'*64,'p'*100]:
            for capacity in [1,9,128]:add(5,first=first,second=second,capacity=capacity)
    for count in [0,1,11,12,13,255]:
        for start in [0,1,11]:add(6,record_data=stored(count,start))
    return rows
if __name__=='__main__':
    (ROOT/'tests/fixtures/data-inputs.json.gz').write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Generated %d Data cases'%len(cases()))
