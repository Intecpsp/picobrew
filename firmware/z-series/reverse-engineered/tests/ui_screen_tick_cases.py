"""Exact ScreenLibrary deadline and dispatch ordering cases."""
import itertools
FIELDS=[
    ('combined',0x2278,1),('combined_caret.enabled',0x1005c,1),('combined_caret.deadline',0x10068,4),('combined_caret.interval_ms',0x1006c,2),
    ('runtime_state.enabled',0x2679,1),('runtime_state.deadline',0x2674,4),
    ('still_transient.enabled',0x26a1,1),('still_transient.deadline',0x269c,4),
    ('pause.enabled',0x2560,1),('pause.deadline',0x255c,4),
    ('runtime_transient.enabled',0x1078,1),('runtime_transient.deadline',0x107c,4),
    ('notification.enabled',0x10b8,1),('notification.deadline',0x10bc,4),
    ('waiting.enabled',0x2621,1),('waiting.animate',0x2620,1),('waiting.frame',0x2619,1),('waiting.frame_count',0x2618,1),('waiting.deadline',0x261c,4),
    ('level.enabled',0x2778,1),('level.deadline',0x277c,4),
    ('paragraph_caret.enabled',0x21fc,1),('paragraph_caret.deadline',0x2208,4),('paragraph_caret.interval_ms',0x220c,2)]

def cases():
    out=[]
    flags=[name for name,_,_ in FIELDS if name.endswith('.enabled') or name.endswith('.animate')]
    def add(now,deadline=1000,**kw):
        state={name:deadline for name,_,_ in FIELDS if name.endswith('.deadline')}
        state.update({'combined_caret.interval_ms':275,'paragraph_caret.interval_ms':500,'waiting.frame_count':4,'waiting.frame':2})
        state.update(kw)
        out.append(dict(now=now&0xffffffff,state=[state.get(name,0)&((1<<(8*size))-1) for name,_,size in FIELDS]))
    for mask,now in itertools.product(range(1<<len(flags)),[1000,1001]):
        add(now,**{name:(mask>>i)&1 for i,name in enumerate(flags)})
    for combined,now,offset in itertools.product([0,1],[0,1,500,0x7fffffff,0xffffff00,0xffffffff],[-1,0,1]):
        add(now,deadline=(now+offset)&0xffffffff,combined=combined,**{name:1 for name in flags})
    for combined,enabled,interval,now in itertools.product([0,1],[0,1],[0,1,500,65535],[1,0xffffffff]):
        add(now,deadline=0,combined=combined,**{'combined_caret.enabled':enabled,'paragraph_caret.enabled':enabled,'combined_caret.interval_ms':interval,'paragraph_caret.interval_ms':interval})
    for count,frame,now in itertools.product([1,2,4,255],[0,1,254,255],[1,0xffffffff]):
        add(now,deadline=0,**{'waiting.enabled':1,'waiting.animate':1,'waiting.frame_count':count,'waiting.frame':frame})
    return out
