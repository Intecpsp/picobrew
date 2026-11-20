#!/usr/bin/env python3
"""Compare real MAX11613 console callbacks through complete OEM return."""
from pathlib import Path
import argparse,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from adc_console_cases import cases
from program_recipe_native import dependency
import run_msp_emulation as common
SOURCES=['src/application_console.cpp','src/application_console_information.inc','src/application_console_manifest.inc',
         'include/zseries/application_console.hpp',
         'src/sensor_acquisition.cpp','src/temperature.cpp','include/zseries/temperature.hpp','include/zseries/sensor_acquisition.hpp',
         'tests/adc_console_probe.cpp','tests/adc_console_cases.py','tests/ghidra/EmulateAdcConsole.java',
         'tests/ghidra/SlideshowExecution.java','tests/test_adc_console.py','tools/validate_adc_console.py']
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def encoded():return (json.dumps(cases(),separators=(',',':'))+'\n').encode()
def native(directory):
    subprocess.run(['make','-s','-j4','json-adapter','ARDUINOJSON_DIR='+str(dependency())],cwd=ROOT,check=True,capture_output=True,text=True)
    binary=Path(directory)/'adc-console'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O1','-Wall','-Wextra','-Werror','-pedantic',
        '-fno-fast-math','-ffp-contract=off','-I'+str(ROOT/'include'),'-I'+str(dependency()/'src'),
        str(ROOT/'tests/adc_console_probe.cpp'),str(ROOT/'build/libzseries_oem_json.a'),'-o',str(binary)],check=True)
    run=subprocess.run([str(binary)],input=''.join(json.dumps(c)+'\n'for c in cases()),capture_output=True,text=True,check=True)
    return [json.loads(line)for line in run.stdout.splitlines()]
def verify_original(original):
    raw=(ROOT.parent/'oem/upstream/zseries_0_0_119.bin').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=original['input_sha256']or hashlib.sha256(encoded()).hexdigest()!=original['input_file_sha256']:raise ValueError('Original image or cases changed')
    segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in common.images.parse_image(raw)['segments']]
    for function in original['original_functions']:
        for span in function['ranges']:
            if hashlib.sha256(common.read_mapped(raw,segments,span['address'],span['size'])).hexdigest()!=span['sha256']:raise ValueError('Executed original code differs')
def compare(original,directory):
    verify_original(original);actual=native(directory)
    if len(actual)!=len(cases())or len(original['cases'])!=len(actual):raise ValueError('Case count mismatch')
    for case,a,b in zip(cases(),actual,original['cases']):
        if a!=b:
            differences=[(i,x,y)for i,(x,y)in enumerate(zip(a['events'],b['events']))if x!=y]
            raise AssertionError((case['id'],a['state'],b['state'],len(a['events']),len(b['events']),differences[:4],a['events'],b['events']))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare-input',type=Path);p.add_argument('--original',type=Path);p.add_argument('--write-evidence',action='store_true');a=p.parse_args()
    if a.prepare_input:a.prepare_input.write_bytes(encoded());print('Prepared',len(cases()),'cases')
    if not a.original:return
    original=json.loads(a.original.read_text())
    with tempfile.TemporaryDirectory(prefix='adc-console-')as directory:compare(original,directory)
    if a.write_evidence:
        fixture=ROOT/'tests/fixtures/adc-console-original.json.gz';fixture.write_bytes(gzip.compress((json.dumps(original,separators=(',',':'))+'\n').encode(),mtime=0))
        report=dict(schema_version=1,input_sha256=original['input_sha256'],input_file_sha256=original['input_file_sha256'],cases=len(cases()),all_results_match=True,
            original_functions=len(original['original_functions']),returning_assertions=original['original_functions_executed'].get('0x400db014',0),
            source_sha256={p:digest(ROOT/p)for p in SOURCES},fixture_sha256=digest(fixture),hardware_tested=False,
            scope='Information, one-shot read and sensor detection callbacks execute through return, with actual ADC, thermistor, enum and returning assertion callees. Every diagnostic, Wire request/read, raw ADC, assertion counter and wrapping clock is compared. SDK Wire results, clock/delay, ROM primitives, compiler arithmetic and finite log/format operations are explicit boundaries; no physical ADC is emulated. Invalid sensor-type indices remain an owned rejection rather than an out-of-bounds OEM access.')
        (ROOT/'analysis/adc-console-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Matched',len(cases()),'complete original MAX11613 console cases')
if __name__=='__main__':main()
