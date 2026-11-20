#!/usr/bin/env python3
"""Independent PicoStill backend request/parser state and stream stimuli."""
import gzip,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def initial():
    b=bytearray(0x440)
    def put(o,v):struct.pack_into('<I',b,o,v&0xffffffff)
    put(0,30000);put(4,0x3ff00000);put(8,0x400d4534);put(16,0x3ff003b4);put(0x154,0x3ff00045);put(0x15c,0x3ff00800);put(0x160,0x3ff00900);put(0x368,511);put(0x3b0,63);put(0x3b8,0x01020304)
    b[0x36c:0x36f]=b'\r\n\0';return b

def cases():
    rows=[]
    def add(op='sync',data=None,action='getFirmwareAddress',key='version',value='0.0.35',uid='001122334455',command=1,incoming=b'',response='',connected=1,connect=1,now=1000,tick=1):
        rows.append(dict(id='%04d'%len(rows),op=op,data=(initial() if data is None else data).hex(),action=action,key=key,value=value,uid=uid,command=command,items=list(incoming),response=response,connected=connected,connect=connect,now=now,tick=tick))
    for connect in (0,1):
        for command in (0,1,2,3):
            for action in ('','getFirmwareAddress','logError','a'*490):add(connect=connect,command=command,action=action)
    for pending_cmd in (1,2,3,0xffffffff):
        b=initial();struct.pack_into('<I',b,0x158,pending_cmd);add(data=b)
    b=initial();b[0x3c]=1;add('send',b)
    for command in (0,1,2,3):
        for response in ('','-1','-2','01','x1','http://host/path','http://host/'+('p'*110)):
            b=initial();struct.pack_into('<I',b,0x158,command);add('complete',b,response=response)
    for command in (1,2,3):
        for text in (b'#-1#',b'HEADERS\r\n#http://host/path#TAIL',b'##',b'#abc#',b'#'+b'x'*255+b'#',b'#'+b'x'*256+b'#',b'unmarked',b'#unfinished'):
            for connected in (0,1):
                b=initial();b[0x3c]=b[0x3d]=1;struct.pack_into('<I',b,0x158,command);struct.pack_into('<I',b,0x40,30000);add('poll',b,command=command,incoming=text,connected=connected)
    for now in (0,29999,30000,30001,0xffffffff):
        for connected in (0,1):
            b=initial();b[0x3c]=b[0x3d]=1;struct.pack_into('<I',b,0x158,1);struct.pack_into('<I',b,0x40,30000);add('poll',b,now=now,connected=connected,tick=0)
    for incoming in (b'HTTP/1.0 200 OK\r\n\r\n#http://example/fw.bin#',b'#-1#',b'#ERR#'):
        for connect in (0,1):add('sequence',incoming=incoming,connect=connect)
    for now in (0xffffff00,0xffffffff):add('sequence',incoming=b'#-1#',now=now)
    return rows
if __name__=='__main__':
    rows=cases();(ROOT/'tests/fixtures/backend-inputs.json.gz').write_bytes(gzip.compress((json.dumps(rows,separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote %d backend cases'%len(rows))
