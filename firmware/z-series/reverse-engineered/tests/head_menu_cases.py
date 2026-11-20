"""Original menu composition, selection pixels and timed scrolling."""
import itertools

def cases():
    out=[]
    def add(labels,operation=0,rows=5,title='',vertical=2,back=False,selected=-1,
            rect=(0,0,127,63),body=False,lines=None,horizontal=1,body_vertical=1,ticks=None):
        out.append(dict(labels=labels,operation=operation,rows=rows,title=title,vertical=vertical,
            back=back,selected=selected,rect=list(rect),body=body,lines=lines or [],
            horizontal=horizontal,body_vertical=body_vertical,ticks=ticks or []))
    for count,rows,vertical,title in itertools.product([1,2,3,5,6,8],[1,3,5],[1,2],['','MENU']):
        add(['Item '+str(i) for i in range(count)],rows=rows,vertical=vertical,title=title,back=count%2==0)
    for count in [2,3,6]:
        labels=['First','A very long selected menu item with Café','Last','Fourth','Fifth','Sixth'][:count]
        for rows,selected in itertools.product([1,3,5],range(count)):
            add(labels,rows=rows,title='Select',selected=selected)
    for rows,body_vertical,horizontal in itertools.product([1,3],[0,1,2],[0,1,2]):
        add(['Back','Continue','Exit'],rows=rows,title='Instructions',body=True,
            lines=['Line one','Line two'],body_vertical=body_vertical,horizontal=horizontal,back=True)
    for ticks in [[802],[803],[803,1603],[803,1604],[803,1604,1629],[803,1604,1630],list(range(803,5500,26))]:
        add(['A very long selected menu item with Café','Next'],rows=1,ticks=ticks)
    for rows in [0,1,2,3,4,5,6]:
        add(['First','Second','Third','Fourth','Fifth','Sixth'],operation=1,rows=rows,rect=(10,18,118,63))
    add(['First','Second'],rows=0)
    add(['','Second'],rows=1)
    return out
