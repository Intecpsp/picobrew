"""Build the real composed Core/console and project the recorded OEM boundaries."""
from pathlib import Path
import json
import os
import shlex
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from program_recipe_native import dependency

def build(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    binary=directory/'application-console';msp=directory/'msp.o'
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-I',str(ROOT/'include'),'-c',str(ROOT/'src/msp_protocol.c'),'-o',str(msp)],check=True)
    sources=sorted((ROOT/'src').glob('*.cpp'))+sorted((ROOT/'adapters/arduinojson').glob('*.cpp'))
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O1','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-ffunction-sections','-Wl,--gc-sections','-I',str(ROOT/'include'),'-I',str(dependency()/'src'),str(ROOT/'tests/application_console_probe.cpp')]+[str(p) for p in sources]+[str(msp),'-o',str(binary)],check=True)
    return binary

def run(binary,cases):
    result=subprocess.run([str(binary)],input=''.join(json.dumps(c)+'\n'for c in cases),text=True,capture_output=True,check=True)
    rows=[json.loads(l)for l in result.stdout.splitlines()]
    if len(rows)!=len(cases):raise RuntimeError('Incomplete native console output')
    return rows

def registry_tree(original):
    shells={}
    for row in original:
        for name,capacity in row['shells']:shells.setdefault(name,dict(name=name,capacity=capacity,entries=[]))
        for entry in row['entries']:
            fields={k:v for k,v in entry.items()if k not in ('shell','callback')}
            shells[entry['shell']]['entries'].append(fields)
    return sorted(shells.values(),key=lambda s:s['name'])

def project(case,native):
    kind=case['kind'];state=native['state'];effects=[]
    for effect in native['effects']:
        if effect[0]in ('log','assertion'):effects.append(effect)
        elif effect[0]=='msp_tx':
            frame=effect[2];width=len(frame)-5
            if frame[:1]!=[8]or frame[-2:]!=[173,222]:raise ValueError('Unexpected MSP frame')
            effects.append(['command',frame[2],int.from_bytes(bytes(frame[3:3+width]),'little'),width])
        elif effect[0]!='delay':raise ValueError(f'Unexpected callback effect {effect}')
    if kind=='duty':value=[state['heater'][3],state['heater'][2]]
    elif kind=='wifi':value=state['wifi'][:1]
    elif kind=='safety':value=state['safety']
    elif kind=='runtime':value=state['runtime'][:3]+[state['runtime_temperature_bits']]
    elif kind=='still':value=state['still']
    elif kind=='t1':value=state['runtime'][4:]
    elif kind=='adc':value=[state['adc']]
    elif kind=='session':value=[state['stop_requested']]
    elif kind=='msp':value=[]
    else:raise ValueError(kind)
    return dict(id=case['id'],value=value,effects=effects,counter=state['counter'],clock=state['clock'])
