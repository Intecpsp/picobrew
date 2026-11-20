"""Every UI command, payload/setting branches, and returning assertions."""
import itertools
import struct
from head_network_cases import parameters,number
from head_core_cases import event

def recipe(seed):return bytes.fromhex(event(0,seed=seed))[16:192].hex()

def cases():
    out=[]
    def add(command=0,language=0,flags=5,index=47,pending=1,debug=0,program=22,abv=5,ibu=6,**state):
        defaults=dict(core=0,session=6,name_length=4,service=0,t1=0,location=0x12345,active=1,duration=43,deadline=85,menu_duration=7000,suspended=0,abv_pending=0,ibu_pending=0,clock=0xfffffff0,counter=19,search_length=6,truncated=1)
        defaults.update(state);p=parameters(program,10);p[1]=abv;p[2]=ibu
        out.append(dict(command=command,language=language,flags=flags,index=index,pending=pending,debug=debug,state=list(defaults.values()),parameters=p+parameters(23,30),manual=[1,0x789ab]+number(151.5,5)+number(1234,6)+number(79,6)+[11,22,33,44],selected=recipe(7),payload=recipe(9),search='search',name='Brew name'))
    for command,language in itertools.product(list(range(176))+[0xffffffff],[0,1,2,0xffffffff]):add(command,language)
    for command,flags,index in itertools.product([4,8,9,10,11,12,21,108,150,151],[0,1,4,5,16,17,31],[0,1,0xffffffff]):add(command,flags=flags,index=index)
    for program in list(range(29))+[0xffffffff]:add(40,program=program)
    for command,value in itertools.product([13,14],[-2147483648,-1,0,5,10,11,2147483647]):add(command,abv=value,ibu=value)
    for core,session,length,duration in itertools.product([0,14],[0,5,6,9,10,0xffffffff],[0,9],[0,7000,0xffffffff]):add(83,core=core,session=session,name_length=length,menu_duration=duration)
    for command,pending,debug in itertools.product([0,4,8,12,19,54,95,100,101,171,174],[0,1],[0,1]):add(command,pending=pending,debug=debug)
    for command,service,t1 in itertools.product([27,42],[0,1],[0,1]):add(command,service=service,t1=t1)
    for command,flags,length in itertools.product([4,19],[1,17],[0,6]):add(command,flags=flags,search_length=length)
    return out
