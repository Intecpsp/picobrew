"""Deterministic returning-domain HTTP cases for original and native replay."""
import gzip,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def record(count=0,start=0):
    data=bytearray(236);data[0x8b]=count;data[0x76]=start
    for i in range(12):struct.pack_into('<ff',data,0x8c+8*i,54.125+i*1.75,-2.625+i*0.625)
    return data.hex()
def cases():
    output=[]
    def add(op,name,first='',second='',body='',wire='',changes=None,stored=None):
        words=[3,1000,1,0,0xffffffff,1,1,1,0,0,17,2,0,0,0,64,97,0,0,0]
        if changes:
            for index,value in changes.items():words[index]=value&0xffffffff
        output.append(dict(id=name,op=op,first=first,second=second,body=body,wire=wire,words=words,record=stored or record()))
    values=['','a','ab','A','same','same text','\xff']
    for op,label in [(0,'equal'),(1,'prefix')]:
        for i,first in enumerate(values):
            for j,second in enumerate(values):add(op,f'{label}-{i}-{j}',first,second)
        for a,b in [(1,0),(0,1),(1,1)]:
            for second in ['', 'a']:add(op,f'{label}-null-{a}-{b}-{len(second)}','a',second,changes={17:a,18:b})
    for index,value in enumerate([0,ord('a'),ord('b'),ord('c'),ord('x'),255]):add(2,f'find-{index}','abc\xff',changes={16:value})
    add(2,'find-null',changes={17:1})
    decoded=['','plain','a+b+c','%20A%2bB','%00x%ff%80','%','%A','%GG','%1g','%g1','%+1','%-1','% 1','%1 ','a%2Fb%3Ac','+'*70,''.join('%%%02x'%i for i in range(256))]
    for i,value in enumerate(decoded):
        for capacity in [1,2,5,64]:add(3,f'decode-{i}-{capacity}',value,changes={15:capacity})
    lines=['GET / HTTP/1.0','POST /setSsid HTTP/1.1','x /a?b=c anything',' / begins','a  two','no_space','method uri','method ','','a\turi\tversion','GET /%2f HTTP/1.1','GET /with\x00tail HTTP/1.0','M '+'x'*255+' V','  /x V','GET / HTTP/1.1 trailing']
    for i,line in enumerate(lines):
        for prior in [0,1]:add(4,f'line-{i}-{prior}',line,changes={8:prior})
    wires=[
        'GET / HTTP/1.0\r\n\r\n','GET / HTTP/1.0\n\n','POST /setSsid HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc',
        'POST /x HTTP/1.0\nContent-Length: 1\n\nabc','GET / HTTP/1.0\ncontent-length: 3\n\nabc',
        'GET / HTTP/1.0\nContent-Length:3\n\nabc','GET / HTTP/1.0\nContent-Length: 0\n\nabc',
        'GET / HTTP/1.0\nContent-Length: -1\n\nabc','GET / HTTP/1.0\nContent-Length: 3stuff\n\nabc',
        'GET / HTTP/1.0\nContent-Length: 1\nContent-Length: 2\n\nabc','GET / HTTP/1.0\nContent-Length: 2\nContent-Length: 1\n\nabc',
        'GET / HTTP/1.0\nContent-Length: nonsense\n\nabc','GET / HTTP/1.0\nContent-Length: 2147483648\n\na',
        'GET / HTTP/1.0\nContent-Length: -2147483649\n\na','GET / HTTP/1.0\nContent-Length:  2\n\nab',
        'GET / HTTP/1.0\nContent-Length: +2\n\na\x00','no_spaces\n\n','method uri\n\n','\n','\r\n','',
        'GET / HTTP/1.0\n','GET / HTTP/1.0\nContent-Length: 3\n\na','GET / HTTP/1.0\nX-'+('h'*520)+'\n\n',
        'M '+('x'*255)+' V\n\n','POST /x V\nContent-Length: 511\n\n'+'b'*511,
    ]
    for i,wire in enumerate(wires):
        add(5,f'parse-{i}',wire=wire)
        add(5,f'parse-disconnect-{i}',wire=wire,changes={5:0})
    for i,settings in enumerate([{3:3},{3:2},{4:5},{4:0},{1:0xfffffffd,0:3},{1:0xffffffff,0:0},{0:0},{2:5},{3:1,0:2}]):add(5,f'parse-clock-{i}',wire=wires[2],changes=settings)
    forms=['ssid=home&pass=password','ssid=a+b&pass=x%2by','ssid=%00x&pass=abc','ssid=&pass=','pass=a&ssid=b','ssid=a','ssid=a&pass','SSID=a&pass=b','ssid=a&ignored=c&x=d','ssid=home&=','ssid=a&x=y=z','ssid=a%26b&pass=%ff%80','ssid='+'s'*100+'&pass='+'p'*100,'ssid='+('s%20'*40)+'&pass='+('%21'*30)]
    for i,body in enumerate(forms):
        for prior in [0,1]:add(6,f'form-{i}-{prior}',body=body,changes={8:prior})
    triggers=['trigger=reset','trigger=RESET','trigger=Reset','trigger=reset%00junk','trigger=reset+','trigger=','trigger=calibrate','trigger=CALIBRATE','trigger=calibrateagain','trigger=wrong','wrong=reset','trigger=%72%65%73%65%74','trigger=reset&x=y','trigger=Calibrate&x=1']
    for op in [7,8]:
        for i,body in enumerate(triggers):
            for expected in ['reset','calibrate']:add(op,f'trigger-{op}-{i}-{expected}',second=expected,body=body)
        add(op,f'trigger-{op}-prior',second='reset',body='trigger=reset',changes={8:1})
        add(op,f'trigger-{op}-null',body='trigger=reset',changes={18:1})
    add(9,'page-header')
    for count in [-2,0,1,2,3,127,128,255]:
        for can,message in [(0,0),(1,0),(0,1),(1,1)]:add(10,f'setup-{count}-{can}-{message}',changes={11:count,12:1,13:can,14:message})
    add(11,'reset-page');add(12,'calibration-page')
    for count,start in [(0,0),(1,0),(3,11),(12,5)]:add(13,f'details-{count}-{start}',stored=record(count,start))
    for i,body in enumerate(forms):add(14,f'configure-{i}',body=body)
    add(14,'configure-prior-error',body=forms[0],changes={8:1})
    def request(uri,body=''):
        return ('POST '+uri+' HTTP/1.0\r\nContent-Length: '+str(len(body))+'\r\n\r\n'+body)
    for uri,body in [('/', ''),('/factoryReset',''),('/calibrate',''),('/details',''),('/setSsid',forms[0]),('/setSsid',forms[3]),('/factoryResetComplete','trigger=RESET'),('/factoryResetComplete','trigger=wrong'),('/calibrateComplete','trigger=calibrateagain'),('/calibrateComplete','trigger=wrong'),('/unknown',''),('/?x=1','')]:
        add(15,'route-'+uri+'-'+body,wire=request(uri,body),stored=record(3,11),changes={13:1,14:1})
    for i,wire in enumerate(['no_spaces\n\n','method uri\n\n','','GET / HTTP/1.0\n','no_spaces\n','\n']):add(15,f'route-malformed-{i}',wire=wire)
    add(15,'route-disabled',wire=request('/'),changes={6:0});add(15,'route-no-client',wire=request('/'),changes={7:0})
    add(16,'cstring-self-tests');add(17,'parser-self-tests')
    return output
def main():
    path=ROOT/'tests/fixtures/http-inputs.json.gz';path.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Wrote',len(cases()),'HTTP cases')
if __name__=='__main__':main()
