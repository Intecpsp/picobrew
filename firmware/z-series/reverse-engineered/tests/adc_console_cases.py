def cases():
    rows=[]
    def add(operation,raw=(2048,2048,2048,2048),types=(0,1,1,2),returned=8,reply=None,clock=1000,counter=7,enabled=True):
        rows.append(dict(id='adc-console-'+str(len(rows)),operation=operation,raw=list(raw),types=list(types),returned=returned,
                         bytes=list(reply if reply is not None else [8,0]*4),clock=clock,counter=counter,enabled=enabled))
    for raw in [(2048,2048,2048,2048),(15,14,4094,4095),(-1,-20,-2147483648,2147483647),(0,0,0,0),(4094,15,1024,3072)]:
        for variant in range(4):add(0,raw,tuple((variant+i)%4 for i in range(4)),enabled=variant%2==0)
    for operation in (1,2):
        for returned in range(10):add(operation,returned=returned)
        for index in range(8):
            reply=[8,0]*4;reply[index]=-1;add(operation,reply=reply)
        for reply in [[0,0]*4,[15,255]*4,[0,15,0,14,15,254,15,255],[0,0,8,0,8,0,8,0],[1,0,8,0,8,0,8,0]]:
            add(operation,reply=reply)
        add(operation,returned=0,clock=0xffffff80,counter=0xffffffff)
    return rows
