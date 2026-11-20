import itertools
def cases():
    out=[]
    for id,value,unit in [(0,15.5,0),(1,195,5),(3,7,7),(4,420,6),(5,160,5),(6,3600,6),(7,300,6)]:
        for metric in [False,True]:
            for quantity in [0,value/2,value,value*2]:out.append(dict(operation=0,id=id,value=quantity,unit=unit,metric=metric,precision=-1,rect=[0,14,127,63]))
    for unit,values in [(0,[0,1,15.5]),(1,[1,4.2]),(2,[0,100,999,1000,2000]),(3,[1,16,32.5]),(4,[0,38,100]),(5,[32,195,212]),(6,[0,59,60,61,3599,3600,86461]),(7,[1,5,15]),(8,[0,4.5])]:
        for metric,value,precision in itertools.product([False,True],values,[-1,0,2]):out.append(dict(operation=1,id=0,value=value,unit=unit,metric=metric,precision=precision,rect=[0,14,127,63]))
    return out
