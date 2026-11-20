"""Synthetic socket/SDK inputs only; original private endpoints stay in OEM."""
import copy
def cases():
    base=dict(id='wifi',words=[100,7,1,3,0,2048,2000,1990,100000,50000,0,0,0,113,1024,0xffffffff,10,0,0],
        wire='HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Type: text/plain\r\n\r\nABC')
    out=[base]
    def add(name,changes=None,wire=None):
        item=copy.deepcopy(base);item['id']=name
        for index,value in (changes or {}).items():item['words'][index]=value
        if wire is not None:item['wire']=wire
        out.append(item)
    add('ethernet',{3:1,4:1});add('fixed-plain-class',{3:2,4:2});add('fixed-secure-class',{4:3})
    for accepted,selected in [(0,0),(0,3),(1,0)]:add('initial-%d-%d'%(accepted,selected),{2:accepted,3:selected})
    for value in [0,700,701]:add('scope-stack-%d'%value,{6:value})
    add('stack-increases',{7:2049});add('counter-wrap',{1:0xffffffff});add('clock-wrap',{0:0xffffff00});add('clock-zero',{0:0})
    for value in [0,60000,60001]:add('heap-before-%d'%value,{8:value})
    for value in [0,15000,15001,19999,20000]:add('heap-after-%d'%value,{9:value})
    for scope in [1,2,3,4,5,7,8]:add('factory-fails-%d'%scope,{10:scope})
    for scope in range(1,6):add('connect-fails-%d'%scope,{11:scope})
    for value in [0,5,0xffffffff]:add('unreachable-errno-%d'%value,{13:value})
    add('unreachable-host-connects',{12:1});add('bytewise-body',{14:1})
    for offset in [0,12,30]:add('header-read-fails-%d'%offset,{15:offset})
    for number in [1,2,3,10]:add('short-write-%d'%number,{17:number})
    add('no-reply-timeout',{16:1000,18:1});add('status-404',wire=base['wire'].replace('200 OK','404 Missing'))
    for length in [-1,0,2,4]:add('content-length-%d'%length,wire=base['wire'].replace('Content-Length: 3','Content-Length: %d'%length))
    add('content-length-absent',wire=base['wire'].replace('Content-Length: 3\r\n',''))
    add('body-truncated',wire=base['wire'][:-1])
    add('empty-body',wire=base['wire'][:-3])
    return out
