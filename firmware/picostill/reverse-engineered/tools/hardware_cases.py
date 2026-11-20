#!/usr/bin/env python3
"""Deterministic actuator/pump-test replay inputs; large fixtures use gzip."""
import gzip
import itertools
import json
from pathlib import Path
from core_cases import bits
ROOT=Path(__file__).resolve().parents[1]


def cases():
    rows=[]
    def add(op, **values): rows.append({"id":"%s-%04d"%(op,len(rows)),"op":op,**values})
    for initial in (0,1):
        add("output", initial=initial, steps=[[0,1],[0,1],[1,0],[1,0],[1,1]])
    initial=[1,12,5,0,0,0,0,0,0,0,0]
    for enabled,duty in itertools.product((0,1),(0,154,155,255)):
        steps=[[0,0,1,0,0],[1,100,1,enabled,duty],[1,101,0,enabled,duty],
               [2,2099,0,0,0],[2,2100,1,0,0],[2,2125,0,0,0],[2,2150,1,0,0]]
        add("fan",words=initial,steps=steps)
    for samples,rpm,pulses,clock in itertools.product((0,4,5,599,600,0xffffffff),(0,1600,2400),(0,1,20),(0,100,0xfffffffe)):
        words=[1,12,5,clock,0,0,1,255,samples,bits(rpm),pulses]
        add("fan",words=words,steps=[[2,clock,1,0,0]])
    add("fan",words=[1,12,5,10,1,0,1,255,5,bits(1600),10],steps=[[2,100,1,0,0]])
    add("fan",words=initial,steps=[[1,0xfffffff0,1,1,255],[2,0xfffffff1,0,0,0],[2,50,1,0,0]])
    for power,first,second in itertools.product((0,1),repeat=3):
        add("fanbank",words=[power,first,second],steps=[[0,100,1,1],[0,101,1,1],[1,102,0,0],[0,103,0,0],[1,104,1,1]])
    for animation in range(39):
        steps=[[0,0,0],[3,10,animation]]+[[4,time,0] for time in (10,259,260,510,1009,1010,2010,3010,5010,10010)]
        add("led",text="",steps=steps)
    for text,repeat in (("",1),("g",1),("gr?y",2),("g.",2),("r-",-1),("g."*40,1),(None,1),("g",0)):
        steps=[[2,0,repeat]]
        if not(text=="g" and repeat==0): steps += [[4,t,0] for t in (0,249,250,1000,2000,4000)]
        add("led",text=text,steps=steps)
    add("led",text="g.",steps=[[2,0,0],[4,0xfffffff0,0],[4,0xfffffff1,0],[4,100,0]])
    for state,pressure,runtime in itertools.product(range(7),(3.9,4,4.1,8.5,8.500001,10,float("nan")),(59999,60000,60001)):
        add("pump_test",state=state,started=[100,0],vacuum=[1000,0],deadline=[60000,0],
            pressures=list(map(bits,[10,6,3])),error=0,
            steps=[[1,runtime+100,0,runtime,0,bits(pressure)]])
    for next_state in (0,1,2,3,4,5,6,0xffffffff):
        add("pump_test",state=0,started=[0,0],vacuum=[0xfffffff0,1],deadline=[0,0],
            pressures=[0,0,0],error=0,steps=[[0,0,0,next_state,0,0]])
    for vacuum,clog,leak in itertools.product((0,1,1000),(3.9,4,4.1),(3.9,4,4.1)):
        add("pump_test",state=4,started=[0,0],vacuum=[vacuum,0],deadline=[0,0],
            pressures=list(map(bits,[10,clog,leak])),error=0,steps=[[1,400000,0,400000,0,bits(8)]])
    add("pump_test",state=0,started=[0,0],vacuum=[0,0],deadline=[0,0],pressures=[0,0,0],error=0,
        steps=[[1,1000,0,0,0,bits(14)],[1,2000,0,1000,0,bits(12.5)],
               [1,62001,0,61001,0,bits(6)],[1,302001,0,301001,0,bits(3)],
               [1,302002,0,301002,0,bits(3)],[1,302003,0,301003,0,bits(9)]])
    return rows


if __name__=="__main__":
    data=(json.dumps(cases(),separators=(",",":"),allow_nan=False)+"\n").encode()
    (ROOT/"tests/fixtures/hardware-inputs.json.gz").write_bytes(gzip.compress(data,mtime=0))
    print("Prepared %d deterministic hardware cases"%len(cases()))
