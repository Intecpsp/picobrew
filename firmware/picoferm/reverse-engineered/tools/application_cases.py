#!/usr/bin/env python3
"""Original PicoFerm owner/scheduler inputs. Raw fields follow the preserved image."""
import gzip,json,random,struct
from pathlib import Path
from core_cases import record
ROOT=Path(__file__).resolve().parents[1]
B=0x3fff01d8

def put(raw,offset,value):struct.pack_into('<I',raw,offset,value&0xffffffff)
def text(raw,offset,value,capacity):
 data=value.encode();assert len(data)<capacity;raw[offset:offset+capacity]=data+b'\0'*(capacity-len(data))
def initial():
 raw=bytearray(0xaf0);put(raw,0,0x401070e0)
 for i,value in enumerate([10000,300000,60000,60000,5000,60000,180000,300,11,0,0]):put(raw,4+4*i,value)
 raw[0x32]=1
 for off,size,value in [(0x38,16,'0.2.7'),(0x48,32,'www.picobrew.com'),(0x68,16,'001122334455'),(0x78,32,'PicoFerm_001122334455'),(0x98,16,'12345678')]:text(raw,off,value,size)
 for off,value in [(0xa8,0x38),(0xac,0x48),(0xb0,0x68),(0xb4,0x78),(0xb8,0x98)]:put(raw,off,B+value)
 raw[0xc0:0x1ac]=record();
 for i in range(12):struct.pack_into('<ff',raw,0x14c+8*i,70.+i,4.+i/10.)
 text(raw,0xc0,'HomeSSID',32);text(raw,0xe0,'WifiPassword',64);raw[0x121]=1;raw[0x122]=10;raw[0x123]=1;raw[0x12d]=1;put(raw,0x125,100000)
 put(raw,0x1b0,B);raw[0x1c4]=42;put(raw,0x1c8,0x40400000)
 off=0x1e8;put(raw,off,30000);put(raw,off+4,B+off);put(raw,off+8,0x402076c4);put(raw,off+0x10,B+off+0x3b8)
 for offset,value in [(0x144,off+0x35),(0x14c,0xc0),(0x150,0x68),(0x154,0x38),(0x158,0x98)]:put(raw,off+offset,B+value)
 put(raw,off+0x360,511);put(raw,off+0x3a8,63);put(raw,off+0x3b8,0x401072a0);put(raw,off+0x3bc,0x04030201)
 put(raw,0x63c,0x3f333333);put(raw,0x640,B);put(raw,0x644,0x40203038)
 raw[0x6e0:0x6e3]=bytes([4,5,12]);put(raw,0x6e4,B);put(raw,0x6e8,0x40203014);put(raw,0x6f8,0xffffffff);put(raw,0x708,1);put(raw,0x710,512)
 off=0x714;put(raw,off,2000);put(raw,off+4,B);put(raw,off+8,0x40205a38);put(raw,off+0x10,B+0x78);put(raw,off+0x1c,B+0xe0);put(raw,off+0x20,B+0xc0);put(raw,off+0x24,1);put(raw,off+0x2c,1);put(raw,off+0x30,0x3ff18000);put(raw,off+0x34,B+0x48)
 put(raw,off+0x38,0x401072a0);put(raw,off+0x3c,0x0104a8c0);put(raw,off+0x40,0x401072a0);put(raw,off+0x44,0x04030201)
 for off,value in [(0x75c,0),(0x760,0xc0),(0x764,0x68),(0x768,0x38),(0x76c,0x48)]:put(raw,off,B+value)
 put(raw,0x7d8,0x401072a0);put(raw,0x7dc,0x0104a8c0);put(raw,0xae8,0);put(raw,0xaec,1000)
 return raw

def cases():
 rows=[];rng=random.Random(0x27a990)
 def add(op,word=None,byte=None,**extra):
  raw=initial()
  for offset,value in (word or {}).items():put(raw,offset,value)
  for offset,value in (byte or {}).items():raw[offset]=value
  row=dict(id='application-%04d'%len(rows),op=op,data='',storage=(bytes(raw[0xc0:0x1ac])+bytes(512-236)).hex(),storage_size=512,now=12345,tick=0,analog=923,gpio=1,arg=0,arg2=0,polls=1,dns=1,update=0,update_error='Update failed',wifi_error='',backend_error='',items=[],connect=[1],connected=1,reads=[],wifi=[0]*18)
  row['wifi'][10:]=[3,0,1,3,1,0x04030201,0x0401a8c0,0x0101a8c0];row.update(extra)
  if 'command'in row:text(raw,0x658,row['command'],128);put(raw,0x6d8,len(row['command']));raw[0x6dc]=1
  if 'url'in row:text(raw,0x5ae,row['url'],128)
  row['data']=raw.hex();rows.append(row)
 for old in range(12):
  for target in range(14):add('transition',word={0x24:old},arg=target)
 for target in [1,4,5,7,11]:
  add('transition',word={0x24:3,0x738:6,0x740:6,0x1cc:2,0x6f8:3},byte={0x31:1,0x654:1},arg=target)
 for session in [0,1,2,99]:
  for restart in [0,1]:add('transition',word={0x2c:session,0x20:0xffffffff},byte={0x34:restart},arg=11)
 for now in [0,99,100,101,200,201,0xffffffff]:
  for duration in [0,100,0xffffffff]:add('timeout',word={0x28:100},now=now,arg=duration)
 for op in ['info','restart','factory_reset','calibrate','calibration','raw_storage','load_storage','persist','led_callback','button_callback']:add(op)
 for length in [0,32,100,105,110,114,235,236]:
  add('raw_storage',storage_size=length);add('load_storage',storage_size=length)
 for previous in range(8):
  for target in range(8):add('wifi_callback',byte={0x7d4:previous&1},arg=previous,arg2=target,dns=target&1)
 for value in [0,1,2,3,-1]:
  for needed,url in [(0,''),(1,''),(1,'http://local/fw.bin')]:add('update',byte={0x5ad:needed},url=url,update=value)
 for error in [0,1]:
  for update in [0,1]:
   for registered in [0,1]:
    for session in [0,1,2,3]:
     for flags in [0,1,64]:add('handle_sync',word={0x630:session,0x634:flags},byte={0x5aa:error,0x5ad:update,0x5ac:registered,0x638:1},url='http://local/fw.bin',update=1)
 commands=['info','requestrestart','battery','voltage','load','save','reset','cal','test','adc','adccont','sync','otau','exit','sleep2','restart','run','setfermenting','setidle','sethaswifi','w ap','w connect','w idle','fakedata','faketimeout','unknown','']
 for command in commands+['led '+str(i)for i in range(11)]+[p+v for p in ['setver ','setweb ','setuid ','setssid ','setpass ']for v in ['', 'new-value','x'*80]]:add('console',command=command)
 for state in range(12):
  for elapsed in [0,10000,60001,300001]:add('poll',word={0x24:state},now=elapsed,analog=200 if elapsed==0 else 923)
 for state in [4,8]:
  for acq in [0,1,6,7,12,13]:
   for logs in [0,11,12]:add('poll',word={0x24:state,0x1cc:acq,0x1d0:0,0x1d8:200000},byte={0x14b:logs},reads=([0]if acq in [1,7]else[32,1,2,3,0]))
 for state in [2,3,5,6,7,9,10]:
  for flag in [0x30,0x31,0x33,0x35,0x654]:add('poll',word={0x24:state},byte={flag:1})
 for state in [5,6,9]:
  for status in [1,5,6]:
   for error in ['', 'Connection lost']:add('poll',word={0x24:state,0x738:status,0x740:6},wifi_error=error)
 for state in [1,2,3]:
  for gpio,pressed,long_reported,deadline in [(0,0,0,0),(0,1,0,12344),(0,1,0,12345),(0,1,1,0),(1,1,0,0)]:add('poll',word={0x24:state,0x650:deadline},byte={0x64c:pressed,0x64d:long_reported},gpio=gpio)
 for mode in [0,1,2]:
  for steps in [-1,0,1,3]:
   for deadline in [0,12345,12346]:add('poll',word={0x24:2,0x6f0:4,0x6f4:mode,0x6f8:steps,0x6fc:250,0x704:deadline,0x70c:1},byte={0x700:1,0x701:1},tick=0)
 for tick in [1,1000,2000,0x7fffffff]:add('poll',word={0x24:2},tick=tick)
 for seconds in [0,2,300,4294,4295,0xffffffff]:add('sleep',arg=seconds)
 setup_reads=[0,1,0,8,0,0,0,0,0,0,0,0,0]+[255,0,255]+[0,0,255,0]+[255]*3
 for op in ['setup','begin']:
  for erased in [False,True]:add(op,word={0x24:11,0x738:0},reads=setup_reads,storage=('ff'*512 if erased else (bytes(initial()[0xc0:0x1ac])+bytes(276)).hex()))
 add('loop',word={0x24:1});add('self_tests')
 return rows
if __name__=='__main__':
 output=ROOT/'tests/fixtures/application-inputs.json.gz';output.write_bytes(gzip.compress((json.dumps(cases(),separators=(',',':'))+'\n').encode(),mtime=0));print('Generated',len(cases()),'original application cases')
