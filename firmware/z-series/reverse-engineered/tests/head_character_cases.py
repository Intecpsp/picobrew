"""Character-picker glyph boundaries and complete original button paths."""
def cases():
    out=[]
    def add(**updates):
        item=dict(operation=0,kind=0,language=0,enabled=True,visible=7,selected=1,
                  limit=14,command=9,text='',truncated=False,rect=[0,14,127,63])
        item.update(updates);out.append(item)
    for kind,count in [(0,13),(1,19),(2,98)]:
        selected=range(count) if kind<2 else [0,1,25,26,51,52,61,62,80,93,94,95,96,97]
        for index in selected:
            add(kind=kind,visible=19 if kind==2 else 7,limit=42 if kind==2 else 14,selected=index)
            add(operation=1,kind=kind,visible=19 if kind==2 else 7,limit=42 if kind==2 else 14,selected=index,text='ABC123' if kind else '123')
        for length in [0,1,13,14,15,21,42,63,64]:
            for index in [0,count-3,count-2,count-1]:
                add(operation=1,kind=kind,visible=19 if kind==2 else 7,limit=42 if kind==2 else 14,selected=index,text=('Ab9'*(length//3+1))[:length])
    for language in [1,2]:
        for index in [94,95,96,97]:add(kind=2,visible=19,selected=index,limit=42,language=language)
    for limit in [0,1,3,14]:
        for text in ['', '1', '123']:
            add(operation=1,limit=limit,text=text)
    for kind in [0,1,2]:add(operation=1,kind=kind,enabled=False,text='unchanged')
    for length in [63,64]:add(operation=1,kind=2,visible=19,limit=42,selected=94,text='x'*length)
    add(operation=1,kind=2,visible=19,limit=42,selected=95,text='x',truncated=True)
    return out
