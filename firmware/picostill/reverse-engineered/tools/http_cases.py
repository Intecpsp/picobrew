#!/usr/bin/env python3
"""Original request/header/body parsing and bounded credential form inputs."""
import gzip
import json
from pathlib import Path
def cases():
    out=[]
    def add(op='parse',text='',other='',capacity=64,now=1000,tick=1,keep=0,error='',items=None,timeout=5000):
        out.append(dict(id='http-%04d'%len(out),op=op,text=text,other=other,capacity=capacity,now=now,tick=tick,keep=keep,error=error,
                        items=items if items is not None else list(text.encode('latin1')),timeout=timeout))
    for request in ('GET / HTTP/1.0\r\n\r\n','POST /configure HTTP/1.1\r\n\r\n','PUT /ping HTTP/1.1\n\n','GET /ping HTTP/1.1\r\nX: a\r\n\r\n',
                    'GET /ping HTTP/1.1\r\ncontent-length: 2\r\n\r\nAB','GET /ping HTTP/1.1\r\nContent-Length:2\r\n\r\nAB',
                    '', 'GET', 'GET /ping', 'GET  HTTP/1.0\n\n','NO_SPACES\n\n','GET /ping HTTP/1.0\nX: '+('a'*530)+'\n\n'):
        for keep in (0,1):add(text=request,keep=keep,tick=1001)
    for length in ('0','1','2','3','10','-1','-2147483648','2147483648','4294967296','garbage',' 2suffix','+2','999999999999999999999999','-999999999999999999999999'):
        for keep in (0,1):add(text='POST /configure HTTP/1.0\r\nContent-Length: '+length+'\r\n\r\nabc',keep=keep,tick=1001)
    for body in ('ssid=Home&pass=pass','ssid=Home+SSID&password=%61%62%63','ssid=&password=','ssid=Only','xssid=bad&x=y','ssid=A&whatever=B&C=D'):
        add(text='POST /configure HTTP/1.1\r\nContent-Length: '+str(len(body))+'\r\n\r\n'+body)
    for size in (0,1,510,511,512,513,520):add(text='POST /data HTTP/1.1\nContent-Length: '+str(size)+'\n\n'+'B'*size)
    for now in (0,0xfffffff0,0xffffffff):
        for tick in (1,1000,5001):add(text='GET / HTTP/1.0\r\n',now=now,tick=tick,keep=1)
    for items in ([71,-1,69,84,32,47,32,72,10,10],[256,511,-1,10,10],[-1]):add(items=items,tick=1001,keep=1)
    for text in ('GET /a HTTP/1.0','GET  HTTP/1.0','no-spaces','GET /a','GET / HTTP/1.1 extra',' GET HTTP/1.0','GET /a?x=2 HTTP/1.0','GET /'+'x'*254+' HTTP/1.0'):
        for error in ('','existing error'):add('request',text,error=error)
    forms=('ssid=Home&pass=Password','ssid=A+B&password=c%20d','ssid=A%00B&pass=x%00y','ssid=&pass=x','ssid=Only','ssid=x&pass','ssid=x&pass=',
           'ssid=x&ignored=y','ssid=x&=y','ssid=x&other=y&password=z','SSID=x&pass=y','x=y&ssid=z','ssid='+'S'*60+'&pass='+'P'*80,'ssid=x&pass=%GG%2%+1%FF')
    for form in forms:
        for error in ('','existing error'):add('form',form,error=error)
    for encoded in ('','abc','A+B%20C','%00x%FF','%','%1','%20','%GG','%+1','%-1','%0x','%x1','abc%41def','%09%0A%0D','Z'*100):
        for capacity in (1,2,3,5,32,64):add('decode',encoded,capacity=capacity)
    for text in ('','0','1','off','on','false','true','OFF','False','0suffix'):add('boolean',text)
    for text in ('','abc','abcd','ABC'):
        for other in ('','abc','a','abcd','ABC'):
            add('equal',text,other);add('prefix',text,other)
    return out
if __name__=='__main__':
    path=Path(__file__).resolve().parents[1]/'tests/fixtures/http-inputs.json.gz';path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote',len(cases()),'HTTP cases')
