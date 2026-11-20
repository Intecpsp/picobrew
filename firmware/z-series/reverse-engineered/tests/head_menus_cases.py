"""Application menus execute through the real original display composition."""
import itertools

def cases():
    out=[]
    def add(**updates):
        value=dict(pause_reason=4,diagnostic_line=0,delta=0,input_kind=0,input_limit=0,response=17,quantity_id=0,quantity_unit=0,quantity_value=8,operation=7,layout=21,title='',detail='',button='',language=0,imperial=False,
            command=3,argument=0,selection=3,strength=5,counter=0,animate=False,percentage=True,
            rows=3,menu_vertical=2,horizontal=1,vertical=1,custom_scroll=False,scroll_command=0,
            scroll_alignment=1,temperatures=[93.25,50.5,100.75],duration=9000,session=3,
            service=False,offline=False,screen_id=91,title_id=19,padding=0,body=True,
            labels=[],back=True,lines=[],scroll=[],network=2,detail_setting=0,static_setting=0,mode=0,ui=30)
        value.update(updates);out.append(value)
    for command,language in itertools.product([7,18,20,27,30,31,32,33,34,35,41,42,43,44,45,92,95,102,107,109,111,115,118,121,126,129,132,134,135,137,150,151,152,169,170,171,173,175],[0,1,2]):add(ui=command,language=language)
    for command,mode,imperial in itertools.product([32,33,34,35,115,121,126,129,132,137],[1,2],[False,True]):add(ui=command,mode=mode,imperial=imperial,detail_setting=1,static_setting=1)
    for command,network,offline in itertools.product([102,109],[0,1,2],[False,True]):add(ui=command,network=network,offline=offline)
    add(ui=42,service=True)
    return out
