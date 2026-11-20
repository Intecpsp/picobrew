#!/usr/bin/env python3
"""Original boot, console queue/receiver and application command stimuli."""
import gzip,json,struct
from pathlib import Path
import application_cases
ROOT=Path(__file__).resolve().parents[1]
def initial():
    b=application_cases.initial()
    for offset,size,text in ((0x18,16,'0.0.35'),(0x28,32,'www.picobrew.com'),(0x48,16,'001122334455'),(0x58,32,'PicoStill_001122334455'),(0x78,32,'home'),(0x98,64,'password'),(0xd8,16,'00000000')):
        b[offset:offset+len(text)+1]=text.encode()+b'\0'
    for offset,pointer in zip(range(0xe8,0x104,4),(0x18,0x28,0x48,0x58,0x78,0x98,0xd8)):struct.pack_into('<I',b,offset,0x3ff00000+pointer)
    struct.pack_into('<I',b,0x14cc,127);struct.pack_into('<I',b,0x1554,127);struct.pack_into('<I',b,0x6fc,127);struct.pack_into('<I',b,0x1000,63)
    return b

def cases():
    rows=[]
    def add(op='dispatch',command='',serial='',data=None,stored_ssid='saved-network',stored_password='saved-pass',mac=0x001122334455,configured=0):
        b=initial() if data is None else data
        if op=='dispatch':
            encoded=command.encode('latin1')[:127];b[0x14d0:0x14d0+len(encoded)]=encoded;struct.pack_into('<I',b,0x1550,len(encoded));struct.pack_into('<I',b,0x1554,127-len(encoded))
        stored=bytearray(100);stored[4:4+len(stored_ssid)+1]=stored_ssid.encode()+b'\0';stored[36:36+len(stored_password)+1]=stored_password.encode()+b'\0'
        rows.append(dict(id='%04d'%len(rows),op=op,command=command,serial=list(serial.encode('latin1')),data=b.hex(),stored=stored.hex(),mac=mac,configured=configured,now=10000))
    for command in ('','unknown','info','setver 1.2.3','setver '+'v'*20,'setweb local-hub','setweb '+'w'*40,'setuid abcdef','setssid kitchen','setssid '+'s'*40,'setpass secret','setpass '+'p'*80,'w ap','w idle','w connect','seterror 0','seterror 6','seterror 100','seterror -1','lp','sp','spp','led g.r.','led ','s 0','s 1','f 0','f 1','p 0','p 1','test v','test c','test p','test f','hw snap','ota'):
        add(command=command)
    for command in ('','info','x'*127,'x'*128,'x'*200):
        add('queue',command)
        b=initial();b[0x14d0:0x14d4]=b'old\0';struct.pack_into('<I',b,0x1550,3);struct.pack_into('<I',b,0x1554,124);add('queue',command,data=b)
    for serial in ('','\n','\r','\0','info','info\n','sp\r\n','x'*126,'x'*127,'x'*128,'a'*5+'\0tail'):
        add('receive',serial=serial)
    b=initial();b[0x14d0:0x14d4]=b'old\0';struct.pack_into('<I',b,0x1550,3);struct.pack_into('<I',b,0x1554,124);add('receive',serial='info\n',data=b)
    for ssid,password in (('',''),('saved-network','saved-pass'),('s'*31,'p'*63)):
        for configured in (0,1):add('setup',stored_ssid=ssid,stored_password=password,configured=configured)
    return rows
if __name__=='__main__':
    rows=cases();(ROOT/'tests/fixtures/machine-inputs.json.gz').write_bytes(gzip.compress((json.dumps(rows,separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote %d machine integration cases'%len(rows))
