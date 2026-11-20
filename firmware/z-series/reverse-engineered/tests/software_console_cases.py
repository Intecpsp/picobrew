def cases():
 return [dict(operation=i,clock=c,water=w,counter=n)for i in range(7)for c,w,n in [(1000,4096,7),(0xffffff80,700,0xffffffff)]]
