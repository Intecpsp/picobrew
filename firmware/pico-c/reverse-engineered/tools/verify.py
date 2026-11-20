#!/usr/bin/env python3
"""Run Pico C reconstruction checks without writing into the preserved OEM tree."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--display', choices=['ssd1325','ssd1309','both'], default='both')
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--host-only',action='store_true')
    group.add_argument('--target-only',action='store_true')
    args=parser.parse_args()
    output=ROOT/'build/verification';output.mkdir(parents=True,exist_ok=True)
    def run(script,*arguments,variant=None):
        env=dict(os.environ)
        if variant:env['PICO_C_OEM_VARIANT']=variant
        subprocess.run([sys.executable,str(ROOT/script),*map(str,arguments)],cwd=ROOT,env=env,check=True)
    if not args.target_only:
        run('tools/inspect_oem.py','--check')
        run('tools/extract_ui_assets.py','--check')
        run('tools/extract_ui_error_text.py','--check')
        run('tests/test_recovered.py')
        for name in ('control','ui_data','ui_graphics','ui_menu','ui_pages','ui_screens','ui_workflows'):
            run('tests/test_'+name+'.py','--output',output/(name+'.json'))
    if not args.host_only:
        variants=['ssd1325','ssd1309'] if args.display=='both' else [args.display]
        for variant in variants:
            fixture=ROOT/'build/verification'/variant/'fixture'
            fixture.mkdir(parents=True,exist_ok=True)
            run('tools/build_core.py','--with-application','--display',variant,'--output-dir',fixture)
            elf=fixture/'application-validation.elf'
            for name in ('controller','phase','device','rfid','recipe_flows','network','network_c','application'):
                run('tests/test_'+name+'.py','--elf',elf,'--output',output/(name+'-'+variant+'.json'),variant=variant)
            run('tests/test_recipe.py','--elf',elf,'--suite','all','--output',output/('recipe-'+variant+'.json'),variant=variant)
            run('tools/build_firmware.py','--display',variant)
            firmware=ROOT/'build/firmware'/variant/'pico-c-recovered.elf'
            for name in ('ui_diagnostics','ui_editor'):
                run('tests/test_'+name+'.py','--elf',firmware,'--variant',variant,'--output',output/(name+'-'+variant+'.json'))
        run('tools/verify_energia_pin_tables.py','--sdk',os.environ.get('PICOBREW_SDK',str(ROOT/'.deps/energia')),
            '--output',output/'board-pin-validation.json')
    print('Pico C reconstruction checks passed. No hardware was exercised.')


if __name__=='__main__':main()
