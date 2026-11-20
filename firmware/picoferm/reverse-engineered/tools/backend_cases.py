#!/usr/bin/env python3
import gzip,json,struct
from pathlib import Path
from data_cases import stored
from core_cases import word,f
ROOT=Path(__file__).resolve().parents[1]
BASE=0x3ff04000
def initial():
    b=bytearray(0x454)
    for offset,value in [(0,30000),(4,BASE),(8,0x402076c4),(16,BASE+0x3b8),(0x144,BASE+0x35),(0x14c,BASE+0x1000),(0x150,BASE+0x1400),(0x154,BASE+0x1500),(0x158,BASE+0x1600),(0x360,511),(0x3a8,63),(0x3b8,0x401072a0),(0x3bc,0x01020304)]:word(b,offset,value)
    b[0x364:0x367]=b'\n\n\0';return b
def cases():
    rows=[]
    def add(op='sync',data=None,record=None,text='isRegistered',key='token',value='12345678',response='',incoming=b'',connect=(1,),connected=1,now=1000,tick=1,rate=5,voltage=f(3.1),logs=1,polls=5,error=''):
        b=initial() if data is None else data
        if error:word(b,0x13c,BASE+0x2300)
        rows.append(dict(id='backend-%04d'%len(rows),op=op,data=b.hex(),record=(stored(2) if record is None else record).hex(),text=text,key=key,value=value,response=response,items=list(incoming),connect=list(connect),connected=connected,now=now,tick=tick,rate=rate&0xffffffff,voltage=voltage,logs=logs,polls=polls,error=error))
    for text in ['', 'www.picobrew.com','h'*63,'h'*100]:add('headers',text=text)
    for text in ['', 'isRegistered','x'*480,'x'*600]:add('prefix',text=text)
    for key,value in [('token','abc'),('', ''),('x'*510,'v'*40)]:add('argument',key=key,value=value)
    for connect in [(1,), (0,1),(0,0,1),(0,0,0,1),(0,)]:add('send',text='GET /path\n',connect=connect)
    b=initial();b[0x2c]=1;add('send',data=b,text='Already pending')
    for error in ['', 'prior error']:add('fail',text='new error',error=error)
    for rate in [0,1,5,0xffffffff]:
        for voltage in [0,f(-1),f(3.1),0x7fc00000]:add(rate=rate,voltage=voltage)
    for count in [0,1,12]:
        for logs in [0,1]:add(record=stored(count),logs=logs)
    for pending in [1,2,5,0xffffffff]:b=initial();word(b,0x148,pending);add(data=b)
    add('state')
    for count in [0,1,11,12,13,255]:
        for start in [0,1,11]:add('logs',record=stored(count,start))
    for command in range(7):
        for response in ['', '1','0','-1','2,0','10,7','http://host/path']:
            b=initial();word(b,0x148,command);add('complete',data=b,response=response)
    for command in [1,2,3,4,5]:
        b=initial();word(b,0x148,command);add('complete',data=b,error='transport failure')
    for command in [1,2,3,4,5]:
        for incoming in [b'#1#',b'HEADERS\r\n#10,7#tail',b'##',b'#'+b'x'*255+b'#',b'#'+b'x'*256+b'#',b'#partial',b'no markers']:
            for connected in [0,1]:
                b=initial();b[0x2c]=b[0x2d]=1;word(b,0x148,command);word(b,0x30,30000);add('poll',data=b,incoming=incoming,connected=connected)
    for now in [0,29999,30000,30001,0xffffffff]:
        for connected in [0,1]:
            b=initial();b[0x2c]=b[0x2d]=1;word(b,0x148,1);word(b,0x30,30000);add('poll',data=b,now=now,tick=0,connected=connected)
    for incoming in [b'#1##0##10,0##10,0#',b'#0##1##http://host/fw.bin##2,1##2,0#',b'#-1##0##invalid##10,1#']:
        for logs in [0,1]:
            for connect in [(1,), (0,1),(0,)]:add('sequence',incoming=incoming,logs=logs,connect=connect)
    return rows
if __name__=='__main__':
    (ROOT/'tests/fixtures/backend-inputs.json.gz').write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Generated %d Backend cases'%len(cases()))
