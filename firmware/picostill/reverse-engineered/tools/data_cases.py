#!/usr/bin/env python3
"""Independent inputs for recovered PicoStill identity and credential handling."""
import gzip,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def initial():
    data=bytearray([0xcc]*208)
    for offset,size,text in ((0,16,'0.0.35'),(16,32,'www.picobrew.com'),(48,16,'001122334455'),(64,32,'PicoStill_001122334455'),(96,32,'home'),(128,64,'password'),(192,16,'00000000')):
        data[offset:offset+len(text)+1]=text.encode()+b'\0'
    return data

def cases():
    rows=[]
    def add(op,text='',other='',number=0,capacity=128,raw='',data=None):
        rows.append(dict(id='%04d'%len(rows),op=op,text=text,other=other,number=number,capacity=capacity,raw=raw,data=(initial() if data is None else data).hex()))
    for firmware in ('','0.0.35','v'*15,'v'*16,'v'*40):
        for host in ('','localhost','h'*31,'h'*32,'h'*64):add('init',firmware,host)
    for value in range(256):add('digit',number=value)
    for capacity in (1,2,4,16,32,64):
        for text in ('','a','ab','x'*(capacity-1),'y'*capacity,'z'*(capacity+2)):add('copy',text,capacity=capacity)
    for count in (0,1,2,4,8,16,32):
        raw=bytes((i*47+11)&255 for i in range(count));add('hex',number=count,capacity=count*2+1,raw=raw.hex())
    for value in ('','A','home','x'*31,'x'*32,'q'*70,'\xff\x80abc'):
        for op in ('ssid','password'):add(op,value)
    for value in ('','001122334455','a'*15,'b'*16,'c'*17,'d'*40):add('uid',value)
    for ssid in ('','home','a'*31,'b'*48,'\xffabc'):
        for password in ('','password','c'*63,'d'*80):add('configure',ssid,password)
    for values in ([0,0,0,0,0],[20,25,30,35,10],[-40,-20,0,80,16],[1.125,2.25,3.375,4.5,8.125]):
        for flags in ((0,0,0,0,0),(1,1,1,1,6),(0,1,1,3,20),(1,0,1,2,99)):
            raw=struct.pack('<5f5I',*values,*flags);add('report',raw=raw.hex())
    return rows
if __name__=='__main__':
    rows=cases();(ROOT/'tests/fixtures/data-inputs.json.gz').write_bytes(gzip.compress((json.dumps(rows,separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote %d identity/credential cases'%len(rows))
