import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def load_native(directory):
    library=Path(directory)/'head-screens.so'
    sources=['src/head_screens.cpp','src/head_hardware.cpp','src/head_still_display.cpp','src/head_slideshow.cpp','src/head_presentation.cpp','src/head_dynamic.cpp','src/head_character.cpp','src/head_quantity.cpp','src/quantity.cpp','src/head_encoder.cpp','src/head_flow.cpp','src/ui_screen_tick.cpp','src/head_menu.cpp','src/head_paragraph.cpp','src/head_widgets.cpp','src/display.cpp','src/display_text.cpp','src/display_fonts.cpp','src/display_effects.cpp','src/ui_catalog.cpp','src/enums.cpp','tests/head_screens_bridge.cpp']
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));words=ctypes.POINTER(ctypes.c_uint32);strings=ctypes.POINTER(ctypes.c_char_p)
    native.head_screens_test.argtypes=[words,ctypes.POINTER(ctypes.c_double),ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,strings,ctypes.c_uint32,strings,ctypes.c_uint32,strings,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint8),words,ctypes.c_void_p,words,ctypes.c_void_p];native.head_screens_test.restype=ctypes.c_uint
    native.head_screens_integration.restype=ctypes.c_uint
    return native

def run(native,case):
    keys=('operation','layout','language','imperial','selection','strength','counter','animate','percentage','rows','menu_vertical','horizontal','vertical','custom_scroll','scroll_command','scroll_alignment','duration','session','service','offline','screen_id','title_id','command','argument','back','body','padding','input_kind','input_limit','response','quantity_id','quantity_unit','pause_reason','diagnostic_line','delta')
    args=(ctypes.c_uint32*len(keys))(*[int(case[key])&0xffffffff for key in keys]);temperatures=(ctypes.c_double*4)(*case['temperatures'],case['quantity_value'])
    arrays=[(ctypes.c_char_p*max(1,len(case[key])))(*[text.encode() for text in case[key]])for key in ('labels','lines','scroll')]
    pixels=(ctypes.c_uint8*4096)();out=(ctypes.c_uint32*80)();menu=ctypes.create_string_buffer(32*129);commands=(ctypes.c_uint32*32)();scroll=ctypes.create_string_buffer(32*65)
    status=native.head_screens_test(args,temperatures,case['title'].encode(),case['detail'].encode(),case['button'].encode(),arrays[0],len(case['labels']),arrays[1],len(case['lines']),arrays[2],len(case['scroll']),pixels,out,menu,commands,scroll)
    if status!=0:raise ValueError('Unexpected native status '+str(status)+' for '+str(case))
    if case['operation']==1:return dict(lines=[bytes(scroll)[i*65:(i+1)*65].split(b'\0',1)[0].decode()for i in range(out[0])],truncated=out[1])
    result=dict(pixels_sha256=hashlib.sha256(bytes(pixels)).hexdigest(),state=list(out)[:46],items=[[commands[i],bytes(menu)[i*129:(i+1)*129].split(b'\0',1)[0].decode()]for i in range(out[46])],lines=[bytes(scroll)[i*65:(i+1)*65].split(b'\0',1)[0].decode()for i in range(out[47])])
    if case['operation']==0 and case['layout']==8:result['character']=list(out)[48:56]
    if case['operation']==0 and case['layout']==12:
        result['quantity']=list(out)[48:55]
        import struct
        result['minimum'],result['step']=struct.unpack('dd',bytes(out)[56*4:60*4])
    if case['operation']==0 and case['layout'] in [0,1,14,16]:result['dynamic']=list(out)[60:70]
    if case['operation']==6:result['diagnostic_line']=out[70]
    return result

def compared(original):return original
