#!/usr/bin/env python3
"""Deterministic sensor acquisition and calibration arithmetic inputs."""
import gzip
import itertools
import json
from pathlib import Path
from core_cases import bits
ROOT=Path(__file__).resolve().parents[1]


def cases():
    rows=[]
    def add(op,**values):rows.append({"id":"%s-%04d"%(op,len(rows)),"op":op,**values})
    points=sorted(set([-1,0,1,14,15,16,1000,2047,2048,4093,4094,4095,4096]+list(range(1,4096,73))))
    for channel,adc in itertools.product(range(4),points):add("convert",channel=channel,adc=adc)
    for three,returned,available in itertools.product((0,1),(0,3,5,6,7,8,9),(0,5,6,8)):
        add("adc",three=three,returned=returned,available=available,bytes=[0xa0,0,0xb0,1,0xc1,0x23,0xdf,0xff])
    for raw in (0,1,15,16,2047,2048,4094,4095):
        add("adc",three=0,returned=8,available=8,bytes=[0xf0|(raw>>8),raw&255]*4)
    add("adc",three=0,returned=8,available=8,bytes=[])
    for status,raw,returned in itertools.product((0,0x40,0x60,0xff),(0,0x199999,0x19999a,0x800000,0xe66666,0xffffff),(0,3,4,5)):
        add("pressure",status=0x40,range=25,returned=returned,available=4,bytes=[status,raw>>16,(raw>>8)&255,raw&255])
    add("pressure",status=0x40,range=25,returned=4,available=0,bytes=[])
    base=dict(values=list(map(bits,[20,30,40,50,12])),rates=list(map(bits,[.25,.5,.75,1,.5])),
              averages=list(map(bits,[5,10,15,20,6])),three=0,available=8)
    for count,returned,raw in itertools.product((0,1,2,59,60,0xffffffff),(0,7,8),(0,1,2048,4095)):
        add("temperatures",**base,counts=[count,50],returned=returned,bytes=[0xa0|(raw>>8),raw&255]*4)
    for count,status,returned in itertools.product((0,1,2,59,60,0xffffffff),(0,0x40),(0,4)):
        add("pressure_sample",**base,counts=[50,count],returned=returned,bytes=[status,0x80,0,0])
    return rows


if __name__=="__main__":
    payload=(json.dumps(cases(),separators=(",",":"),allow_nan=False)+"\n").encode()
    (ROOT/"tests/fixtures/sensors-inputs.json.gz").write_bytes(gzip.compress(payload,mtime=0))
    print("Prepared %d sensor/calibration cases"%len(cases()))
