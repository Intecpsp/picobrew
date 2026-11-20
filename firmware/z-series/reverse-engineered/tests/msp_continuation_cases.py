"""Explicit device responses for complete original MSP command continuations."""
import struct

ZERO='000000000000000000'
BOOT='515151515100000000'
APP='010100000000000000'
UNIT='01ffff000000000000'
BAD='010203000000000000'
STATUS='02f3091a2befbeadde'
PRODUCTION_CHUNKS=176+4+1+1
UNIT_CHUNKS=126+1+4+1+1

def state(id=2,moving=0):
 value=bytearray(range(0x90,0x90+24));struct.pack_into('<I',value,4,id);value[20]=moving;return value.hex()

def update_responses(chunks=PRODUCTION_CHUNKS,version=0xa1,erase_fail=None,program_fail=None):
 result=[bytes([version]).hex()]
 if version&0xf0!=0xa0:return result
 for index in range(30):
  value=5 if index==erase_fail else 0;result.append(bytes([value]).hex())
  if value:return result
 for index in range(chunks):
  value=7 if index==program_fail else 0;result.append(bytes([value]).hex())
  if value:return result
 return result

def setup_responses(first=BOOT,boot_version=0xa1,after=APP,final=APP,**update):
 result=[first]
 if first[:10]==BOOT[:10]:pass
 elif first.startswith('01'):result.append(ZERO)
 else:return result
 result.append(bytes([boot_version]).hex())
 if boot_version&0xf0!=0xa0:return result
 result.append(after)
 if after!=APP:result += [ZERO]+update_responses(**update)+[final]
 return result

def test_update_responses(iterations,unit_version=UNIT,final=APP):
 result=[]
 for _ in range(iterations):
  result += [ZERO]+update_responses(chunks=UNIT_CHUNKS)+[unit_version]
  result += setup_responses(first=unit_version,after=unit_version,final=final)
 return result

def cases():
 output=[]
 def add(name,operation,responses=(),**values):
  row=dict(name=name,operation=operation,before=state(),responses=list(responses),clock=1000,counter=0,water=1024,command=13,value=0xdeadbeef,value_bytes=0,position=1,address=0xc002,data='',corrupt_crc=False)
  row.update(values);output.append(row)
 for id in (0,1,2,3,0xffffffff):
  for moving in (0,1):
   for command,width in ((13,1),(253,0),(200,4)):
    add(f'send-{id}-{moving}-{command}','send',[STATUS],before=state(id,moving),command=command,value_bytes=width,counter=0xffffffff,clock=0xffffff80)
 for position in (0,1,9,10,255):
  for id,moving in ((2,0),(2,1),(0,1)):
   add(f'valve-{position}-{id}-{moving}','valve',[STATUS],position=position,before=state(id,moving))
 for id in (0,1,2,3,0xffffffff):add(f'start-{id}','start',before=state(id))
 for response in (APP,ZERO,BOOT,STATUS,'510000000000000000','ff0102030405060708','020000000000000000','02a7000000a0a1a2a3'):add('version-'+response,'version',[response],before=state(0,1))
 for fail in (None,0,14,29):
  responses=['00']*30 if fail is None else ['00']*fail+['85']
  add(f'erase-{fail}','erase',responses,before=state(1),counter=0xffffffff,clock=0xffffff80)
 for length in (0,1,16,17,33):
  for failure in (None,0,1):
   chunks=(length+15)//16;responses=['07'if i==failure else '00'for i in range(chunks)]
   if failure is not None and failure<chunks:responses=responses[:failure+1]
   add(f'program-{length}-{failure}','program',responses,data=bytes((i*29+7)&255 for i in range(length)).hex(),address=0xfffffc)
 for id in (1,2):
  for version,erase,program in ((0x91,None,None),(0xa1,0,None),(0xa1,29,None),(0xa1,None,0),(0xaf,None,1),(0xa0,None,None)):
   add(f'update-{id}-{version}-{erase}-{program}','update',update_responses(chunks=3,version=version,erase_fail=erase,program_fail=program),before=state(id))
 for id in (0,2):
  add(f'setup-unrecognized-{id}','setup',[ZERO],before=state(id))
  add(f'setup-crc-{id}','setup',[ZERO],before=state(id),corrupt_crc=True)
 for first in (BOOT,APP):
  add('setup-healthy-'+first,'setup',setup_responses(first=first),before=state(0))
  add('setup-incompatible-'+first,'setup',setup_responses(first=first,boot_version=0x90),before=state(0))
 for after in (ZERO,BOOT,UNIT):
  for final,kw in ((APP,{}),(BAD,{'version':0x90}),(BAD,{'erase_fail':0}),(APP,{'program_fail':1})):
   add(f'setup-recovery-{after}-{final}-{kw}','setup',setup_responses(after=after,final=final,**kw),before=state(0))
 add('test-version-200','test_version',[APP]*200)
 add('test-version-200-assertions','test_version',[ZERO]*200,before=state(2,1),water=700,counter=0xffffffff,clock=0xffffff80)
 add('test-switch-50','test_switch',[item for _ in range(50)for item in (ZERO,APP)])
 add('test-update-ten','test_update',test_update_responses(10))
 add('test-update-prior-assertion','test_update',test_update_responses(1),counter=7)
 add('test-update-signed-counter','test_update',test_update_responses(10),counter=0xffffffff)
 add('test-update-unit-assertions','test_update',test_update_responses(1,unit_version=BAD))
 add('test-update-production-assertions','test_update',test_update_responses(1,final=BAD))
 for fail in (False,True):
  responses=[ZERO]+(['05']if fail else ['00']*30)+setup_responses(after=BOOT)
  add(f'test-bootstrap-{fail}','test_bootstrap',responses)
  add(f'test-errors-{fail}','test_errors',[ZERO,ZERO]if fail else [STATUS,'020000000000000000'])
 for counter in (0,7):
  responses=[ZERO]+['00']*30+setup_responses(after=BOOT)
  responses += [item for _ in range(10)for item in (ZERO,APP)]+[APP]*10
  responses += test_update_responses(2 if counter==0 else 1)+[STATUS,'020000000000000000']
  add(f'test-integration-{counter}','test_integration',responses,counter=counter)
 return output
