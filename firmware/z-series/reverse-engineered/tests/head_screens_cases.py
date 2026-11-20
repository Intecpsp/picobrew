"""Complete original screen-layout calls and Head presentation helpers."""
import itertools
import json
from pathlib import Path
import run_msp_emulation as common

def cases():
    out=[]
    def add(**updates):
        case=dict(pause_reason=4,diagnostic_line=0,delta=0,input_kind=0,input_limit=0,response=17,quantity_id=0,quantity_unit=0,quantity_value=8,operation=0,layout=21,title='TITLE',detail='',button='',language=0,imperial=False,
            command=3,argument=0,selection=3,strength=5,counter=0,animate=False,percentage=True,
            rows=3,menu_vertical=2,horizontal=1,vertical=1,custom_scroll=False,scroll_command=0,
            scroll_alignment=1,temperatures=[93.25,50.5,100.75],duration=9000,session=3,
            service=False,offline=False,screen_id=91,title_id=19,padding=0,body=True,
            labels=['Back','Continue','Exit'],back=True,lines=['First line','A123','Last line'],
            scroll=['Line '+str(i) for i in range(10)])
        case.update(updates);out.append(case)
    for layout,title in itertools.product([3,6,7,9,10,11,13,15,17,18,19,20,21,22,99],['','TITLE']):
        add(layout=layout,title=title,scroll=['First','Second','Third'] if layout==19 else ['Line '+str(i) for i in range(10)])
    for value,imperial in itertools.product([-5,50.5,93.25,120],[False,True]):add(layout=7,imperial=imperial,temperatures=[value,50.5,100.75])
    for value in [0,1,6,7,65535]:add(layout=11,selection=value)
    for value in [0,1,9,10]:add(layout=10,strength=value)
    for count,animate in itertools.product([0,1,-1,255,256],[False,True]):add(layout=13,counter=count,animate=animate,lines=['Please wait','Working now'])
    for command in [0,3,47]:add(layout=21,command=command)
    for custom,button,command in [(True,'',0),(True,'Back',1),(False,'Ignored',3)]:add(layout=6,custom_scroll=custom,button=button,scroll_command=command)
    for language,service,offline in itertools.product([0,1,2],[False,True],[False,True]):add(operation=2,language=language,service=service,offline=offline)
    for language,screen in itertools.product([0,1,2],[58,77,81,91,93,98,100,102]):add(operation=3,language=language,screen_id=screen)
    for detail,animate,counter in itertools.product(['','Network detail'],[False,True],[0,10,256]):add(operation=4,detail=detail,animate=animate,counter=counter)
    for token in ['ABCD','123456',''] :add(operation=5,detail=token)
    for kind,limit,title,language in itertools.product([0,1,2],[7,14],['','TITLE'],[0,2]):add(layout=8,input_kind=kind,input_limit=limit,title=title,language=language)
    for kind,value,unit in [(0,8,0),(1,195,5),(3,7,7),(4,420,6),(5,160,5),(6,3600,6),(7,300,6)]:
        for imperial,title in itertools.product([False,True],['','TITLE']):add(layout=12,quantity_id=kind,quantity_value=value,quantity_unit=unit,imperial=imperial,title=title)
    for layout,title,imperial,language in itertools.product([0,1,14,16],['','TITLE'],[False,True],[0,2]):add(layout=layout,title=title,imperial=imperial,language=language)
    for reason in [0,1,2,4,7,12,15,18]:add(layout=16,pause_reason=reason,title='',labels=[])
    add(operation=6)
    for index in range(22):add(operation=6,diagnostic_line=index+1)
    for line,delta in [(1,-10),(22,10),(3,-1),(6,2)]:add(operation=6,diagnostic_line=line,delta=delta)
    text_values=['','Words fit','One\ntwo\nthree','word '*80,'ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJK tail',' Leading  multiple   spaces \n new line','Café crème and more words after the boundary.']
    raw=common.INPUT.read_bytes();parsed=common.images.parse_image(raw)
    segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in parsed['segments']]
    assets=json.loads((Path(__file__).resolve().parents[1]/'analysis/ui-assets.json').read_text())
    addresses=sorted({int(p,16)for screen in assets['screens']for p in screen['pointers']})
    for address in addresses:
        value=bytearray();index=0
        while True:
            byte=common.read_mapped(raw,segments,address+index,1)
            if byte==b'\0':break
            value+=byte;index+=1
        text_values.append(value.decode('utf-8'))
    for text in text_values:add(operation=1,title=text)
    return out
