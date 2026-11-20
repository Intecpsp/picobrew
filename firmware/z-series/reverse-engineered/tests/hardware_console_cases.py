def cases():
 return [dict(operation=operation,clock=1000 if water==1024 else 0xffffff80,counter=7 if water==1024 else 0xffffffff,water=water)for operation in (0,1)for water in (1024,700)]
