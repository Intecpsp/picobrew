"""Prescribed HTTP responses for the complete original backend network test."""
import copy
import json
from pathlib import Path
import re


def documents():
    source=(Path(__file__).resolve().parents[1]/'src/backend_console_documents.inc').read_text()
    values={int(index):json.loads(literal) for index,literal in
            re.findall(r'backend_test_document_(\d+)\[\] = (".*");',source)}
    registration=dict(IsRegistered=True,TokenExpired=False,ZBackendError=0,
        RegistrationToken='fixture',UpdateAddress='-1',ProgramUri='',Alias='fixture',
        SessionStats=dict(DirtySessionsSinceClean=0,LastSessionType=16,ResumableSessionID=-1))
    recipe_list=dict(Recipes=[dict(ID=i+1,Name='Fixture '+str(i+1),Kind=0,Abv=1,Ibu=2)for i in range(11)],Offset=0,TotalResults=11)
    pico=json.loads(values[6]);pico['PicoRecipeID']=1
    zymatic=json.loads(values[2]);zymatic['ID']=146530
    return [registration,recipe_list,dict(Recipes=[],Offset=0,TotalResults=0),
        dict(Recipes=recipe_list['Recipes'][:2],Offset=0,TotalResults=2),
        dict(ID=1,Name='Not Yet-ie Imperial Stout',Kind=1,Abv=1,Ibu=2),pico,
        dict(ID=146530,Name='Atomic Amber',Kind=0,Abv=1,Ibu=2),zymatic,json.loads(values[7])]


def cases():
    result=[]
    variants=['normal','counter_wrap','low_stack','invalid_json','wrong_values','factory_failure',
              'connect_failure','content_type','http_status','short_content','empty_uid','same_uid']
    for index,name in enumerate(variants):
        payloads=copy.deepcopy(documents());clock=1000;counter=7;water=4096
        if name=='counter_wrap':clock=0xffffff00;counter=0xfffffffa
        if name=='low_stack':water=700
        if name=='wrong_values':
            payloads[0].update(IsRegistered=False,TokenExpired=True,ZBackendError=3)
            payloads[1].update(Offset=3,TotalResults=7);payloads[1]['Recipes']=payloads[1]['Recipes'][:2]
            payloads[3]['Recipes']=[];payloads[4].update(ID=2,Name='Wrong',Kind=0)
            payloads[6].update(ID=1,Name='Wrong',Kind=1);payloads[8]=dict(SessionType=0)
        wires=[]
        for item in payloads:
            body='{' if name=='invalid_json' else json.dumps(item,separators=(',',':'))
            length=len(body)+(1 if name=='short_content'else 0)
            status='503 Unavailable'if name=='http_status'else'200 OK'
            content='text/plain'if name=='content_type'else'application/json'
            wires.append('HTTP/1.1 '+status+'\r\nContent-Type: '+content+'\r\nContent-Length: '+str(length)+'\r\nConnection: close\r\n\r\n'+body)
        result.append(dict(id=name,clock=clock,counter=counter,water=water,uid=''if name=='empty_uid'else'fixture00001',
            same_uid=name=='same_uid',factory=0 if name=='factory_failure'else 1,connect=0 if name=='connect_failure'else 1,
            yield_ms=200,source=3,pressure=0,maximum=96,wires=wires))
    return result
