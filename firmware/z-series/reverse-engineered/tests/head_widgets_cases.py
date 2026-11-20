"""Concrete original widget painters, including full packed framebuffer output."""
import itertools

def cases():
    out=[]
    def add(operation,rect=(0,14,127,63),value=0,flag=0,language=0,first='Status',second='',lines=None,font=4,horizontal=1,vertical=1,padding=0):
        out.append(dict(operation=operation,rect=list(rect),value=value,flag=flag,language=language,first=first,second=second,lines=['Ready','Please wait'] if lines is None else lines,font=font,horizontal=horizontal,vertical=vertical,padding=padding,seed=17,counter=7))
    for first,underline in itertools.product(['','SHORT','12345678901234','123456789012345','A VERY LONG TITLE STRING','Café'],[0,1]):add(0,first=first,flag=underline)
    for rect,value,percent in itertools.product([(0,0,127,63),(0,14,127,63),(8,25,119,55)], [0,1,9,10,50,99,100,101,255],[0,1]):add(1,rect=rect,value=value,flag=percent)
    for rect,value,metric in itertools.product([(0,0,127,63),(0,14,127,63)],[-50,0,21,99,100,212],[0,1]):add(2,rect=rect,value=value,flag=metric)
    for rect,seconds in itertools.product([(0,0,127,63),(0,14,127,63)],[0,59,60,3599,3600,9000,359999,360000]):add(3,rect=rect,value=seconds)
    for rect,value,language in itertools.product([(0,0,127,63),(0,14,127,63)],[0,1,5,9,10,0xffffffff],[0,1,2]):add(4,rect=rect,value=value,language=language)
    for rect,detail,animate,frame in itertools.product([(0,0,127,63),(0,14,127,63)],['','Working now'],[0,1],range(4)):add(5,rect=rect,second=detail,flag=animate,value=frame)
    for rect,location in itertools.product([(0,0,127,63),(0,14,127,63)],range(9)):add(6,rect=rect,value=location)
    for rect,font,horizontal,vertical in itertools.product([(0,0,127,63),(0,14,127,63)],[0,4],[0,1,2],[0,1,2]):add(7,rect=rect,font=font,horizontal=horizontal,vertical=vertical,padding=2)
    for lines in [[],[''],['','Hidden'],['One',''],['012345678901234567890123456789','Café'],['abc']*8]:add(7,lines=lines)
    add(1,rect=(8,45,119,63),value=50,flag=1)
    add(1,rect=(0,0,127,3),value=50,flag=0)
    add(3,rect=(0,0,84,63),value=3600)
    return out
