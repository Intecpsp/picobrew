#!/usr/bin/env python3
"""All OEM local routes, parser-driven forms, status objects and full pages."""
import gzip
import json
import struct
from pathlib import Path
def cases():
    out=[]
    def add(uri='/ping',body='',flags=None,scan=2,raw=None,history=3,error=0):
        factory=bytearray(860);struct.pack_into('<IIQ',factory,0,error,0,123456789)
        struct.pack_into('<5f',factory,16,1,.75,.5,.25,0);struct.pack_into('<5f',factory,36,20.25,31.75,5.5,-10.25,13.2)
        for i in range(history):struct.pack_into('<f',factory,56+4*i,i*.125-1.5)
        struct.pack_into('<I',factory,856,history)
        text=raw if raw is not None else ('POST ' if body else 'GET ')+uri+' HTTP/1.0\r\n'+('Content-Length: '+str(len(body))+'\r\n' if body else '')+'\r\n'+body
        out.append(dict(id='server-%04d'%len(out),request=text,flags=flags or [1,1,0,0,1,1,0,0x0a01a8c0],
                        strings=['0.0.35','HomeSSID','PicoManual','68.00,70.00,71.00,72.00,13.20,0,0,0,None','None'],
                        ssids=['HomeSSID','OtherSSID'],scan=scan,factory=factory.hex(),now=1000,tick=1))
    routes=('/','/home','/wifi','/data-page','/ping','/configure','/stillip','/start','/end','/needsupdate','/connecttolan','/data','/version','/hardwareError',
            '/factoryResult','/fanon','/fanoff','/pumpon','/pumpoff','/fantoggle','/fanstate','/sensors','/clean','/runningprogram','/factoryreset','/stillstate','/unknown')
    for uri in routes:
        for active in (0,1):add(uri,body='ssid=Home+Wifi&password=pass%21' if uri=='/configure' else '',flags=[active]*7+[0x0a01a8c0])
    for uri in routes[4:]:add(uri+'?extra=yes')
    for body in ('ssid=Home&pass=x','ssid=&pass=x','ssid=Home','ssid=Home&unexpected=value','ssid='+('S'*60)+'&pass='+('P'*80),'bad=a&pass=b','ssid=A%00B&pass=%FF'):
        add('/configure',body)
    for scan in (-2,-1,0,1,2):add('/',scan=scan)
    for history in (0,1,2,199,200):
        for error in (0,1,6,20,21):add('/factoryResult',history=history,error=error)
    for raw in ('GET /ping','BAD\n\n','GET  HTTP/1.0\n\n','GET /ping HTTP/1.0\nX: a\n\n','POST /configure HTTP/1.0\nContent-Length: 20\n\nssid=x&pass=y'):
        add(raw=raw)
    for flags in ([0]*8,[1,0,1,1,0,0,1,0],[1,1,1,1,1,1,1,0xffffffff]):add('/stillstate',flags=flags)
    return out
if __name__=='__main__':
    path=Path(__file__).resolve().parents[1]/'tests/fixtures/http-server-inputs.json.gz';path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote',len(cases()),'HTTP route cases')
