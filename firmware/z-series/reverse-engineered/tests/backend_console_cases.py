"""Deterministic full diagnostic suites, markers and backend event encoders."""
def cases():
 result=[]
 for clock,counter,water in [(1000,0,4096),(0xffffff50,0xfffffffa,700),(0x80000101,3,699),(17,0xffffffff,701)]:
  for operation in range(2):result.append(dict(operation=operation,clock=clock,counter=counter,water=water))
 for category in range(5):
  for clock,counter,elapsed,failed in [(1000,17,25,0),(0xfffffffa,0xffffffff,20,1),(42,3,1234567,2)]:
   result.append(dict(operation=2,clock=clock,counter=counter,water=4096,category=category,elapsed=elapsed,failed=failed))
 for error in [0,1,0xff,0x100,0xffff,0x7fffffff,0x80000000,0xffffffff]:
  result.append(dict(operation=3,clock=1000,counter=0,water=4096,argument=error))
 for mask in range(16):
  result.append(dict(operation=4,clock=1000,counter=0,water=4096,argument=mask,second=(mask*17)|((255-mask*17)<<8)|(((mask*37)&255)<<16)))
 return result
