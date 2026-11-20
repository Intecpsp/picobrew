#!/usr/bin/env python3
"""Compare Z119 font metrics, UTF-8 behavior and complete rendered framebuffers."""
import argparse
import base64
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from validate_display import IMAGE_SHA,digest,pattern

ROOT=Path(__file__).resolve().parents[1]
OPERATIONS=['utf8','glyphs','text','metrics']

def case(name,op,text=b'',font=4,**extra):
    if isinstance(text,str):text=text.encode('utf-8')
    return dict(id=name,op=op,seed=29,font=font,spacing=0,bounds=[0,0,127,63],
                text_base64=base64.b64encode(text).decode(),**extra)

def input_cases():
    cases=[]
    samples=[b'',b'PicoBrew',b'\t\n\r',bytes([127]),'Éléphant','ÿ','λ','🍺',
             ''.join(chr(i) for i in range(32,256)),b'a\0ignored',b'\x80',b'\xff',b'\xfe',b'\xc3A',b'\xe1AA',
             b'\xf0\x9fAA',b'\xc0\x80',b'\xc1\xbf',b'\xe0\x80\x80',b'\xf0\x80\x80\x80',
             b'\xc3',b'\xe1\x80',b'\xf0\x9f\x8d',b'\xf7\xbf\xbf\xbf',b'\xed\xa0\x80']
    for index,text in enumerate(samples):cases.append(case(f'utf8-{index}','utf8',text))
    for font in range(5):
        for index,bounds in enumerate([[0,0,127,63],[5,10,75,45],[-4,-3,63,35],[0,0,0,0]]):
            for spacing in [0,2,20]:
                item=case(f'metrics-{font}-{index}-{spacing}','metrics',font=font);item.update(bounds=bounds,spacing=spacing);cases.append(item)
        glyphs=list(range(32,257))+[9,10,13,0x3bb,0x1f37a] if font==4 else [9,10,13,32,33,48,65,77,97,127,128,193,223,233,255,256,0x1f37a]
        for n,codepoint in enumerate(glyphs):
            item=case(f'glyph-{font}-{codepoint}','glyphs',chr(codepoint),font,characters=1,point=[n%3,7],invert=n%2);cases.append(item)
        for index,text in enumerate(['PicoBrew','A citrus pale ale for friends','Éléphant à côté de la forêt','🍺 beer',b'\x80abc',b'A\xc3']):
            for alignment in [0,1,2]:
                item=case(f'text-{font}-{index}-{alignment}','text',text,font,options=[0,index%3,alignment%2,alignment,0,-1])
                item['spacing']=index%2;cases.append(item)
        for n,(text,options,bounds) in enumerate([
            ('Shifted label',[0,1,0,2,3,2],[4,5,90,40]),
            ('Small centered text',[5,0,1,1,0,0],[5,8,90,40]),
            ('Text below the screen',[0,12,0,0,0,0],[0,0,127,63]),
            ('Before screen',[0,-1,0,0,0,0],[0,0,127,63]),
            ('Oversized requested count',[40,1,0,0,3,0],[0,0,127,63]),
            ('',[0,0,0,0,0,0],[0,0,127,63])]):
            item=case(f'text-options-{font}-{n}','text',text,font,options=options);item['bounds']=bounds;cases.append(item)
    return cases

def load_native(directory):
    compiler=shlex.split(os.environ.get('CXX','c++'));library=Path(directory)/'display_text.so'
    sources=['src/display.cpp','src/display_fonts.cpp','src/display_text.cpp','tests/display_text_bridge.cpp']
    subprocess.run(compiler+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-I',str(ROOT/'include')]+[str(ROOT/s) for s in sources]+['-o',str(library)],check=True)
    native=ctypes.CDLL(str(library));native.display_text_test.argtypes=[ctypes.c_uint,ctypes.POINTER(ctypes.c_uint32),ctypes.c_char_p,ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)];native.display_text_test.restype=ctypes.c_int
    return native,subprocess.check_output(compiler+['--version'],text=True).splitlines()[0]

def native_case(native,item):
    args=[0]*24;args[:6]=[item['font'],item['spacing'],*item['bounds']];args[6:12]=item.get('options',[0]*6)
    args[12:14]=item.get('point',[0,0]);args[14]=item.get('characters',0);args[15]=item.get('invert',0)
    text=base64.b64decode(item['text_base64'])+b'\0';pixels=(ctypes.c_uint8*4096).from_buffer_copy(pattern(4096,item['seed'],73))
    values=(ctypes.c_uint32*24)();points=(ctypes.c_uint32*512)()
    result=native.display_text_test(OPERATIONS.index(item['op']),(ctypes.c_uint32*24)(*(x&0xffffffff for x in args)),text,len(text),pixels,values,points)
    if result:raise AssertionError(f'Native text fixture failed: {item["id"]}: {result}')
    errors={0:'',6:'empty',7:'truncated',8:'overlong'}
    if values[0] not in errors:raise AssertionError(f'Unexpected native text error {item["id"]}: {values[0]}')
    return {'id':item['id'],'error':errors[values[0]],'pixels_sha256':hashlib.sha256(bytes(pixels)).hexdigest(),
        'metrics':list(values[6:19]),'length':values[2],'codepoints':list(points[:values[1]]),
        'decoder':list(values[3:6]) if item['op']=='utf8' else []}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare-input',type=Path);parser.add_argument('--original-jsonl',type=Path)
    parser.add_argument('--input',type=Path);parser.add_argument('--export',type=Path);parser.add_argument('--write-evidence',action='store_true');args=parser.parse_args()
    cases=json.loads(args.input.read_text()) if args.input else input_cases()
    if args.write_evidence and args.input:parser.error('Evidence requires full deterministic cases')
    if args.prepare_input:args.prepare_input.write_text(json.dumps(cases,separators=(',',':'))+'\n');print(f'Prepared {len(cases)} text cases')
    if not args.original_jsonl:return 0
    execution=json.loads(Path(str(args.original_jsonl)+'.json').read_text());originals=[json.loads(line) for line in args.original_jsonl.read_text().splitlines()]
    input_hash=digest(args.input) if args.input else hashlib.sha256((json.dumps(cases,separators=(',',':'))+'\n').encode()).hexdigest()
    if digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin')!=IMAGE_SHA or execution['input_sha256']!=IMAGE_SHA or execution['input_file_sha256']!=input_hash:
        raise SystemExit('Image/input execution identity mismatch')
    if execution['cases']!=len(cases) or len(originals)!=len(cases):raise SystemExit('Original execution case count mismatch')
    with tempfile.TemporaryDirectory(prefix='z-display-text-') as directory:
        native,compiler=load_native(directory)
        for item,expected in zip(cases,originals):
            actual=native_case(native,item)
            if actual!=expected:
                print(json.dumps({'case':item,'expected':expected,'actual':actual},indent=2));raise SystemExit('Text mismatch')
    sources=['include/zseries/display_text.hpp','include/zseries/display_fonts.hpp','src/display.cpp','src/display_fonts.cpp','src/display_text.cpp',
             'tests/display_text_bridge.cpp','tools/ghidra/DisplayExecution.java','tools/ghidra/EmulateDisplayText.java','tools/validate_display_text.py','tools/extract_display_fonts.py']
    report={'schema_version':1,'input_sha256':IMAGE_SHA,'cases':len(cases),'all_results_match':True,'scope':execution['scope'],
        'original_functions_executed':execution['original_functions_executed'],'modeled_boundaries':execution['modeled_boundaries'],
        'original_execution_jsonl_sha256':digest(args.original_jsonl),'compiler':compiler,'hardware_tested':False,
        'source_sha256':{p:digest(ROOT/p) for p in sources},'bounded_assertion_cases':sum(bool(x['error']) for x in originals)}
    if args.export:report['original_instruction_sha256']={a:digest(args.export/'instructions'/('fn_'+a[2:]+'.json')) for a in report['original_functions_executed']}
    if args.write_evidence:
        if not args.export:parser.error('--write-evidence requires --export')
        fixture=ROOT/'tests/fixtures/display-text-original.json';fixture.write_text(json.dumps({'input_sha256':IMAGE_SHA,'cases':[{'input':i,'original':o} for i,o in zip(cases,originals)]},indent=2)+'\n')
        report['golden_fixture_sha256']=digest(fixture);(ROOT/'analysis/display-text-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'All {len(cases)} original/native text cases match; {report["bounded_assertion_cases"]} explicit assertion-to-error cases')
    return 0
if __name__=='__main__':raise SystemExit(main())
