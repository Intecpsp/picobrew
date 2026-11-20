"""Text routing and owned Buffer16/Buffer64 copy cases; synthetic inputs only."""
import itertools
def padded(value,size):return list(value)+[0]*(size-len(value))
def atoi32(text):
    text=text.split(b'\0',1)[0].lstrip(b' \t\n\r\v\f');sign=1
    if text[:1] in (b'+',b'-'):
        if text[:1]==b'-':sign=-1
        text=text[1:]
    value=0
    for byte in text:
        if not 48<=byte<=57:break
        value=value*10+byte-48
    value*=sign
    if not -2147483648<=value<=2147483647:raise ValueError('Outside verified ROM atoi result domain')
    return value&0xffffffff
def cases():
    out=[]
    def add(command,pending,text,tail=0):
        state=[pending]+padded(b'fixture-network',33)+padded(b'old-password',65)+[len(out)%2]+padded(b'old-kit',17)+[7,len(out)%2]
        value=padded(text,65)
        if tail:
            zero=value.index(0)
            for i in range(zero+1,65):value[i]=tail
        out.append(dict(command=command,state=state,text=value,atoi=atoi32(bytes(value))))
    for command,pending in itertools.product(list(range(176))+[0xffffffff],[0,1]):add(command,pending,b'12345')
    for command,pending,length in itertools.product([0,6,16,17],[0,1],range(65)):add(command,pending,b'x'*length)
    numbers=[b'',b'0',b'1',b'-1',b'+1',b'2147483647',b'-2147483648',b' \t+42tail',b'\r\n-42',b'0000012',b'1.5',b'0x123',b'xyz',b'--1',b'+-2',b'\xff42']
    for command,text in itertools.product([16,17],numbers):add(command,0,text)
    for command,pending,tail in itertools.product([0,6,16,17],[0,1],[1,255]):add(command,pending,b'123\0unused',tail)
    return out
