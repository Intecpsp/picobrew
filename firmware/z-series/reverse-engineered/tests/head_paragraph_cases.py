"""Scroll layout, footer rendering and caret blinking against original pixels."""
import itertools

def cases():
    out=[]
    def add(rect=(0,14,127,63),alignment=1,command=3,button='Exit',reserve=False,
            lines=None,offset=-1,carets=0):
        out.append(dict(rect=list(rect),alignment=alignment,command=command,button=button,
            reserve=reserve,lines=lines or [],offset=offset,carets=carets))
    for rect,alignment,footer,reserve,count in itertools.product(
            [(0,0,127,63),(0,14,127,63)],[0,1,2],[(0,''),(3,'Exit'),(0,'Empty command')],[False,True],[0,2,10]):
        add(rect=rect,alignment=alignment,command=footer[0],button=footer[1],reserve=reserve,
            lines=['Line '+str(i) for i in range(count)])
    for offset,carets in itertools.product([0,1,4,6],[0,1,2]):
        add(lines=['Café','Short','','A longer line of scrolling text','Fifth','Sixth','Seventh','Eighth','Ninth','Tenth'],offset=offset,carets=carets)
    return out
