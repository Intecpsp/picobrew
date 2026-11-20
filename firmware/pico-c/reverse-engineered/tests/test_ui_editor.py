#!/usr/bin/env python3
"""Compare C rotary text/adjustment UI with both original instruction images."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from ui_linked_support import Pair, Layout, ROOT, argument


def graphics(p):
    p.hook('pico_display_clear_rows', count=2)
    p.hook('pico_display_character_at_pixels', count=3)
    p.hook('pico_display_character_on_row', 0x20005744, count=3)
    for native, original, count in [
        ('pico_graphics_fill_rectangle','GFX_fill_rectangle',5),
        ('pico_graphics_fill_triangle','GFX_fill_triangle',7),
        ('pico_graphics_text_color','GFX_set_text_color',1),
    ]:
        p.hook(native, original, count=count, old_shift=1, new_shift=1)
    def bitmap(r, side):
        width, height, color = [argument(r, i) for i in (4,5,6)]
        p.event(side, 'bitmap', r.reg(1), r.reg(2), width, height, color,
                r.read(r.reg(3), ((width + 7)//8) * height).hex())
    p.old.hook(p.entry(0x20012f64), lambda r: bitmap(r, 'old'))
    p.new.hook_symbol('pico_graphics_bitmap', lambda r: bitmap(r, 'new'))


def verify(variant, elf):
    layout = Layout(elf)
    traces = []
    def editor(selections, capacity=33, initial=b''):
        p=Pair(variant,elf,layout)
        p.drawing(); graphics(p)
        clicks=[];positions=[]
        for index in selections:
            clicks += [0,1]
            positions += [index,index]
        p.hook('pico_encoder_take_click', sequence=clicks)
        p.hook('pico_encoder_get_position', sequence=positions)
        for arm in (p.old,p.new):
            arm.write(arm.SCRATCH,b'\0'*256)
            arm.write(arm.SCRATCH,initial+b'\0')
        events=p.invoke('pico_encoder_edit_text',0x2000a888,args=[p.old.SCRATCH,capacity])
        if p.old.read(p.old.SCRATCH,256)!=p.new.read(p.new.SCRATCH,256):
            raise AssertionError(('text result',variant,selections,capacity))
        new_encoder=p.ui+layout.offset('PicoUi','encoder')
        for member,offset,kind in [('minimum',16,'i'),('maximum',20,'i'),('position',32,'i'),('wrap_enabled',40,'B')]:
            original=p.old.read(0x2002a428+offset,struct.calcsize(kind))
            new=p.new.read(new_encoder+layout.offset('PicoEncoder',member),len(original))
            if original != new:
                raise AssertionError(('encoder state',member,original.hex(),new.hex()))
        traces.append(['text',selections,capacity,initial.hex(),p.old.read(p.old.SCRATCH,256).hex(),events])
    for selected in range(99):
        editor([selected,1])
    for capacity in (1,2,8,17,33):
        editor([4,5,6,3,98,1],capacity)
        editor([*range(4,30),3,3,3,1],capacity)
        editor([3,3,1],capacity,b'previous')
    for baseline in (0,1,4,10):
        for stored in (-1,0,1,5,10):
            for positions in ([0,0],[-1,-1],[9,9],[8,3]):
                p=Pair(variant,elf,layout)
                p.drawing();graphics(p)
                p.hook('pico_display_adjustment_box',0x2000ab74,count=2)
                # Two non-click iterations and a final click; preserve separate
                # original volatile reads of the final value at confirmation.
                p.hook('pico_encoder_take_click',sequence=[0,0,1])
                p.hook('pico_encoder_get_position',sequence=[*positions,*([positions[-1]]*5)])
                for arm in (p.old,p.new):
                    arm.write(arm.SCRATCH,struct.pack('<i',stored)+b'\x5a'*4)
                events=p.invoke('pico_encoder_select_adjustment_level',0x2000abfc,
                    args=[0,baseline,p.old.SCRATCH,p.old.SCRATCH+4],
                    new_args=[baseline,p.new.SCRATCH,p.new.SCRATCH+4],compare_return=True)
                if p.old.read(p.old.SCRATCH,8)!=p.new.read(p.new.SCRATCH,8):
                    raise AssertionError(('adjustment state',baseline,stored,positions))
                traces.append(['adjustment',baseline,stored,positions,p.old.read(p.old.SCRATCH,8).hex(),events])
    for custom in (0,1):
        for style in (0,1,2,3):
            for index in (0,1,5,9,10,255):
                p=Pair(variant,elf,layout);p.drawing();graphics(p)
                p.old.write_u32(0x2002a400+0x1c,custom)
                p.new.write_u32(p.ui+layout.offset('PicoUi','display.font'),custom)
                events=p.invoke('pico_display_adjustment_box',0x2000ab74,args=[style,index])
                traces.append(['box',custom,style,index,events])
    for celsius in (0,1):
        for row in (0,1,3,-1):
            for values in ((149.5,320.,158.),(31.99,32.,0.),(-40.,-10.,-9.),(212.,194.,33.)):
                p=Pair(variant,elf,layout);p.drawing()
                p.old.write(0x2002c127,bytes([celsius]))
                p.write('settings','PicoSettings','temperature_unit_celsius',celsius,'B')
                words=struct.unpack('<6I',struct.pack('<3d',*values))
                events=p.invoke('pico_display_temperature_columns',0x2000a358,
                    args=[row&0xffffffff,0,*words[:2]],stack=words[2:])
                traces.append(['temperatures',celsius,row,values,events])
    return {'variant':variant,'cases':len(traces),'trace_sha256':hashlib.sha256(json.dumps(traces).encode()).hexdigest(),
            'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'original_code_executed':True,
            'rebuilt_arm_code_executed':True,'hardware_tested':False,
            'limits':['Drawing and scripted encoder-input boundaries; electrical timing not simulated',
                      'Valid original character indices and adjustment ranges only; explicit bounds repairs are separate']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--variant',choices=['ssd1325','ssd1309'],required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();report=verify(args.variant,args.elf)
    text=json.dumps(report,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text,end='')


if __name__=='__main__':main()
