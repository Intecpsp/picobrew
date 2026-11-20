#!/usr/bin/env python3
"""Independent updater states, headers, transport results and URL inputs."""
import gzip,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def cases():
    rows=[]
    def add(op='poll',state=0,length=10,binary=1,written=0,now=1000,changed=0,lines=(),body=b'',connect=1,begin=1,end=1,finished=1,error=7,read_limit=2048,write_limit=2048,uri='http://host/path.bin',polls=1):
        rows.append(dict(id='%04d'%len(rows),op=op,state=state,length=length,binary=binary,written=written,now=now,changed=changed,lines=list(lines),body=body.hex(),connect=connect,begin=begin,end=end,finished=finished,error=error,read_limit=read_limit,write_limit=write_limit,uri=uri,polls=polls))
    for uri in ('http://host/path','http:///path','http://h/','https://host/path','0123456host/path','http://'+'h'*63+'/path','http://'+'h'*64+'/path','http://h/'+('p'*63),'http://h/'+('p'*80)):
        add('url',uri=uri)
    for connect in (0,1):add('begin',connect=connect)
    for state in (0,6,7,0xffffffff):add(state=state)
    for now in (0,4999,5000,5001,0xffffffff):
        for body in (b'',b'X'):add(state=1,now=now,body=body)
    for lines in ((),('HTTP/1.1 200 OK','Content-Length: 10','Content-Type: application/octet-stream',''),('HTTP/1.0 200 OK','Content-Length: 10','Content-Type: application/octet-stream',''),('HTTP/1.1 404 Not Found',),('HTTP/1.1 400 200','Content-Length: 10','Content-Type: application/octet-stream'),('Content-Length: 0','Content-Type: application/octet-stream'),('Content-Length: -1','Content-Type: application/octet-stream'),('Content-Type: text/plain','Content-Length: 3'),('content-length: 10','Content-Type: application/octet-stream'),('Content-Type:  application/octet-stream','Content-Length: 10'),(' Content-Length: 10 \r','Content-Type: application/octet-stream\r','\r'),('Content-Length: 2147483648','Content-Type: application/octet-stream')):
        for previous in (0,1):add(state=2,length=10 if previous else 0,binary=previous,lines=lines)
    for begin in (0,1):
        for length in (0,1,2048,-1):add(state=3,length=length,begin=begin)
    for length in (1,10,2048,2049,4096):
        for size in (0,1,5,2048,2050):add(state=4,length=length,body=bytes(i&255 for i in range(size)))
    for written in (9,10,11,0xffffffff):add(state=4,written=written,body=b'01234',read_limit=2,write_limit=1)
    for end in (0,1):
        for finished in (0,1):add(state=5,end=end,finished=finished)
    add('sequence',length=0,binary=0,lines=('HTTP/1.1 200 OK','Content-Length: 10','Content-Type: application/octet-stream',''),body=b'0123456789',polls=7)
    return rows
if __name__=='__main__':
    rows=cases();(ROOT/'tests/fixtures/update-inputs.json.gz').write_bytes(gzip.compress((json.dumps(rows,separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote %d updater cases'%len(rows))
