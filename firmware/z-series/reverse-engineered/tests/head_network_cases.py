"""Wi-Fi suppression/localization and Azure group-session cases."""
import itertools
import struct
def number(value,unit):return list(struct.unpack('<II',struct.pack('<d',value)))+[unit]
def parameters(program,seed):
    return [program,seed,seed+1]+number(seed+2,6)+number(seed+3,0)+number(seed+4,7)+number(seed+5,4)+[1]
def cases():
    out=[]
    def add(operation,core=0,language=0,event=0,argument=0,second=0,payload=None):
        out.append(dict(operation=operation,core=core,language=language,event=event,argument=argument,second=second,
                        state=parameters(24,10),payload=parameters(8,30) if payload is None else payload))
    cores=list(range(17))+[0xffffffff]
    for core,event,language in itertools.product(cores,list(range(7))+[0xffffffff],[0,1,2,0xffffffff]):
        add(0,core,language,event,3,0xfffffffe)
    for core,error,language in itertools.product(cores,list(range(6))+[0xffffffff],[0,1,2,0xffffffff]):
        add(0,core,language,1,error,7)
    for event,program in itertools.product(list(range(5))+[0xffffffff],list(range(29))+[0xffffffff]):
        add(1,event=event,argument=program)
    for program,water_unit,temperature_unit,value in itertools.product(range(20,24),[0,1],[4,5],[0,1,-12.5,100,1000]):
        p=parameters(8,30);p[6:9]=number(value,water_unit);p[12:15]=number(value,temperature_unit)
        add(1,event=2,argument=program,payload=p)
    for water_unit,temperature_unit in itertools.product(list(range(9))+[0xffffffff],repeat=2):
        p=parameters(8,30);p[8]=water_unit;p[14]=temperature_unit
        add(1,event=2,argument=20,payload=p)
    for low,high in [(0,0x80000000),(0x12345,0x7ff80000),(0,0x7ff00000),(0,0xfff00000)]:
        p=parameters(8,30);p[3:6]=[low,high,0xffffffff];p[9:12]=[low,high,0xffffffff]
        add(1,event=2,argument=23,payload=p)
    return out
