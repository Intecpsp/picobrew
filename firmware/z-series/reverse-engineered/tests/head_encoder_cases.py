"""Deterministic inputs for the original Head encoder and inline selectors."""
import itertools

FIELDS = [
    ("combined",0x5b20,1), ("menu",0x5b14,1), ("menu_selected",0x5b00,4),
    ("menu_count",0x10004,4), ("menu_visible",0x5b04,4), ("menu_row",0x5af4,4), ("menu_offset",0x5af8,4),
    ("slideshow",0x5a30,1), ("slide_selected",0x5a28,4), ("slide_count",0x5a2c,4),
    ("slide_timer",0x5a3c,1), ("slide_duration",0x5a40,4), ("slide_deadline",0x5a44,4),
    ("paragraph",0x5aa4,1), ("paragraph_offset",0x5a94,4), ("paragraph_visible",0x5a98,4), ("paragraph_lines",0x1a508,4),
    ("character",0x5b5c,1), ("character_selected",0x5b70,4), ("character_count",0x5b68,4),
    ("temperature",0x5c00,1), ("temperature_value",0x5c04,4), ("temperature_min",0x5c0c,4), ("temperature_max",0x5c08,4),
    ("duration",0x5c49,1), ("seconds",0x5c4c,4), ("strength",0x5c88,1), ("strength_value",0x5c8c,4),
    ("quantity0",0x5cd0,1), ("wrap0",0x5cd1,1), ("selected0",0x5cd4,4), ("count0",0x5cd8,4),
    ("quantity1",0x5d78,1), ("wrap1",0x5d79,1), ("selected1",0x5d7c,4), ("count1",0x5d80,4),
    ("diagnostic",0x9f78,1), ("combined_selected",0x5b21,1), ("paragraph_count",0x5b22,1), ("combined_menu_count",0x5b23,1),
    ("transient",0x9f80,1), ("transient_duration",0x9f84,4), ("transient_deadline",0x9f88,4), ("dirty",0x2205c,1)]


def cases():
    result=[]
    defaults=dict(menu_selected=2,menu_count=7,menu_visible=3,menu_row=1,menu_offset=1,
                  slide_selected=2,slide_count=4,slide_duration=4000,slide_deadline=12,
                  paragraph_offset=3,paragraph_visible=4,paragraph_lines=20,
                  character_selected=5,character_count=13,temperature_value=65,temperature_min=20,temperature_max=100,
                  seconds=3600,strength_value=5,selected0=3,count0=8,selected1=4,count1=12,
                  combined_selected=1,paragraph_count=3,combined_menu_count=7,transient_duration=7000,transient_deadline=14)
    def add(delta,clock=0xffffff00,**kw):
        state=dict(defaults);state.update(kw)
        result.append(dict(delta=delta&0xffffffff,clock=clock,state=[state.get(name,0)&((1<<(8*size))-1) for name,_,size in FIELDS]))
    widgets=['menu','slideshow','paragraph','character','temperature','duration','strength','quantity0','quantity1']
    for mask,delta in itertools.product(range(512),[-1,1]):
        add(delta,transient=1,slide_timer=1,**{key:(mask>>i)&1 for i,key in enumerate(widgets)})
    deltas=[-2147483648,-99,-13,-2,-1,1,2,13,99,2147483647]
    add(0,menu=1,transient=1)
    for count,selected,visible,delta in itertools.product([0,1,7,0xffffffff],[0,1,6,0xffffffff],[0,3],deltas):
        add(delta,menu=1,menu_count=count,menu_selected=selected,menu_visible=visible)
    for count,delta,active in itertools.product([1,2,12,0xffffffff],deltas,[0,1]):
        for selected in [0,1,count-1]:add(delta,slideshow=1,slide_count=count,slide_selected=selected,slide_timer=active,transient=active)
    for total,visible,offset,delta in itertools.product([0,1,20,-1],[0,1,5],[0,1,19],[-99,-1,1,99]):
        add(delta,paragraph=1,paragraph_lines=total,paragraph_visible=visible,paragraph_offset=offset)
    for count,delta in itertools.product([0,1,13,19,98],deltas):
        for selected in [-1,0,count-1,count]:add(delta,character=1,character_count=count,character_selected=selected)
    for bounds,value,delta in itertools.product([(20,100),(100,20),(-2147483648,2147483647)],[-2147483648,-1,0,65,2147483647],deltas):
        add(delta,temperature=1,temperature_min=bounds[0],temperature_max=bounds[1],temperature_value=value)
    for seconds,delta in itertools.product([0,1,59,60,61,3599,3600,3601,10799,10800,10801,43199,43200,43201,259199,259200,0x7fffffff,0xffffffff],deltas):
        add(delta,duration=1,seconds=seconds)
    for value,delta in itertools.product([-2,0,1,5,9,10,11,2147483647,-2147483648],deltas):
        add(delta,strength=1,strength_value=value)
    for index,count,wrap,selected,delta in itertools.product([0,1],[0,1,8],[0,1],[-1,0,1,7],deltas):
        add(delta,**{f'quantity{index}':1,f'count{index}':count,f'wrap{index}':wrap,f'selected{index}':selected})
    for first,second,selected,delta in itertools.product([0,1,3,250],[0,1,6,250],[0,1,5,254],[-99,-1,1,99]):
        add(delta,combined=1,paragraph_count=first,combined_menu_count=second,combined_selected=selected,menu=1,temperature=1,transient=1)
    for diagnostic,delta,dirty in itertools.product([0,1,22],[-1,1],[0,1]):
        add(delta,diagnostic=diagnostic,dirty=dirty,transient=1)
    # A returning zero-delta assertion still rounds duration and performs the
    # enabled redraw/timer operations. These are not inert event payloads.
    add(0,duration=1,seconds=3601,transient=1)
    add(0,slideshow=1,slide_timer=1,character=1,temperature=1,strength=1,transient=1)
    return result
