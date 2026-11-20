"""Core/Still mailbox, state presentation and returning-assertion cases."""
import itertools
import struct

def event(kind,argument=0,previous=0,seed=0):
    raw=bytearray(0xdf0)
    def w(offset,value):struct.pack_into('<I',raw,offset,value&0xffffffff)
    w(4,kind);w(8,argument);w(12,previous)
    for index in range(20):
        p=16+176*index;name=('Recipe %d/%d'%(seed,index)).encode()
        w(p,seed*100+index);raw[p+4:p+4+len(name)]=name;w(p+72,len(name));w(p+76,64-len(name))
        raw[p+80]=index%2;raw[p+81]=(index//2)%2;w(p+84,index%4)
        w(p+88,index);w(p+92,seed);w(p+96,seed+index)
        for off in (16,32,48,64):
            struct.pack_into('<d',raw,p+88+off,seed+index+off/10);w(p+88+off+8,index%9)
        raw[p+168]=index%2
    for off,value in ((0xdd0,17),(0xdd4,20),(0xdd8,57),(0xde0,seed%5),(0xde4,seed%3),(0xde8,seed),(0xdec,seed%55)):w(off,value)
    raw[0xddc]=seed%2
    return raw.hex()

def cases():
    out=[]
    def add(kind=0,argument=0,previous=0,language=0,operation=0,pending=1,debug=0,seed=0,**state):
        defaults=dict(runtime=0,received=250,total=1000,interrupted=0,maintenance=0,limit=10,date=1234,active=0,duration=43,deadline=85,prompt=30000,progress=0,dirty=0,rinse=0,clock=0xfffffff0,counter=19)
        defaults.update(state)
        out.append(dict(operation=operation,language=language,pending=pending,debug=debug,event=event(kind,argument,previous,seed),state=list(defaults.values()),token='REG-123',network='network details'))
    for language,current,previous in itertools.product([0,1,2,0xffffffff],list(range(19))+[0xffffffff],[0,7,11]):add(argument=current,previous=previous,language=language)
    for interrupted,maintenance,limit,rinse in itertools.product([-1,0,1],[0,10,255],[0,10,255],[0,1]):add(argument=12,previous=7,interrupted=interrupted,maintenance=maintenance,limit=limit,rinse=rinse)
    for received,total in itertools.product([-2147483648,-1,0,1,100,101,255,256,2147483647],[-1,0,1,100,2147483647]):add(argument=8,received=received,total=total)
    for language,kind in itertools.product([0,1,2,0xffffffff],list(range(17))+[0xffffffff]):add(kind,language=language,seed=kind&7)
    for runtime,active,prompt in itertools.product(list(range(6))+[0xffffffff],[0,1],[0,1,30000,0xffffffff]):add(12,runtime=runtime,active=active,prompt=prompt)
    for pending,debug,kind in itertools.product([0,1],[0,1],[3,8,13]):add(kind,pending=pending,debug=debug,seed=kind)
    for pending,debug,kind,state in itertools.product([0,1],[0,1],[0,1,2,0xffffffff],[0,1,2,7,0xffffffff]):add(kind,state,operation=2,pending=pending,debug=debug)
    return out
