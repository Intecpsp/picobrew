#!/usr/bin/env python3
"""Exercise real embedded browser assets against authenticated local API fixtures.

Requires Playwright and CHROMIUM_PATH. No device or cloud service is contacted.
"""
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
KEY = '0123456789abcdef' * 4
FIELDS = ['elapsed_ms','wort_f','thermoblock_f','target_f','step_seconds','phase','controller_status','compartment','current_step','flags','device_state','temperature_f','pressure_kpa','battery_v','sensor_pressure_native']

class Quiet(SimpleHTTPRequestHandler):
    def log_message(self, *_args): pass

def main():
    output = Path(os.environ.get('WEB_TEST_OUTPUT', '/tmp/pico-common-browser'))
    output.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=str(ROOT / 'web')))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    checks = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'), args=['--no-sandbox'])
            for model in ['picoferm','picostill','z-series']:
                for width in [1280,390]:
                    context = browser.new_context(viewport={'width':width,'height':900})
                    page = context.new_page()
                    errors, requests, commands = [], [], []
                    page.on('pageerror', lambda error: errors.append(str(error)))
                    brewing = model == 'z-series'
                    allowed = ['brew','manual','clean','drain','chill','stop','pause','resume','encoder','resume-interrupted','discard-interrupted'] if brewing else ['start','stop','calibrate','sample','set-sampling-interval'] if model == 'picoferm' else ['start','essential-oils','clean','stop','set-pump','set-fan']
                    descriptors = {name:{'allowed':[],'required':[]} for name in allowed}
                    for name,fields in {'brew':['recipe_id'],'encoder':['delta','click'],'calibrate':['confirm'],'resume-interrupted':['confirm'],'set-sampling-interval':['value'],'set-pump':['enabled'],'set-fan':['channel','level','enabled']}.items():
                        if name in descriptors: descriptors[name]={'allowed':fields,'required':[] if name=='encoder' else fields[:2] if name=='set-fan' else fields}
                    info = {'api_version':1,'mode':'standalone','model':model,'model_name':{'z-series':'Pico Z','picoferm':'PicoFerm','picostill':'PicoStill'}[model],'model_family':model,'firmware_version':'1.2.0-fixture','commands':allowed,'command_fields':descriptors,'settings_fields':['temperature_unit','language','detailed_status'] if brewing else ['temperature_unit'],'recipe_capacity':16 if brewing else 0,'step_capacity':15 if brewing else 0,'front_panel':brewing,'telemetry_forwarding':True,'capabilities':{'recipes':brewing,'brewing':brewing,'heating':brewing,'front_panel':brewing,'temperature':True,'pressure':not brewing,'battery':model=='picoferm','fermentation':model=='picoferm','pump':model!='picoferm','fans':model=='picostill'}}
                    sensors = [{'id':'temperature','label':'Temperature','kind':'temperature','unit':'F','value':68.5},{'id':'pressure_native','label':'Pressure','kind':'pressure','unit':'raw','value':532.125},{'id':'battery','label':'Battery','kind':'voltage','unit':'V','value':3.8}] if model=='picoferm' else [{'id':'inlet','label':'Inlet','kind':'temperature','unit':'F','value':152.1},{'id':'outlet','label':'Outlet','kind':'temperature','unit':'F','value':145.2},{'id':'pot','label':'Pot','kind':'temperature','unit':'F','value':170.4},{'id':'ambient','label':'Ambient','kind':'temperature','unit':'F','value':68.1},{'id':'pressure','label':'Pressure','kind':'pressure','unit':'kPa','value':6.89}]
                    actuators=[] if model!='picostill' else [{'id':'pump','label':'Pump','kind':'pump','enabled':False,'level':None,'rpm':None},{'id':'fan1','label':'Fan 1','kind':'fan','enabled':True,'level':25,'rpm':1200},{'id':'fan2','label':'Fan 2','kind':'fan','enabled':False,'level':0,'rpm':0}]
                    status={'model':model,'model_name':info['model_name'],'model_family':model,'ready':True,'busy':False,'stopped':False,'state':'idle','device_uid':'fixture-device','command':'idle','device_state':0,'phase':4,'current_step':-1,'step_count':0,'step_seconds':0,'wort_f':72 if brewing else None,'thermoblock_f':73 if brewing else None,'target_f':0 if brewing else None,'temperature_f':68.5,'pressure_kpa':None if model=='picoferm' else 6.89,'battery_v':3.8 if model=='picoferm' else None,'filter_inserted':None,'sensors':[] if brewing else sensors,'actuators':actuators}
                    history={'available':True,'id':1,'state':'completed','command':'start','record_count':3,'persisted_record_count':3,'duration_ms':30000,'first_elapsed_ms':0,'gap_count':0,'truncated':False,'fields':FIELDS[:-1]+['sensor_current_schema']}
                    newer=dict(history,id=2,sample_interval_ms=300000)
                    older=dict(history,id=1,sample_interval_ms=60000)
                    history.update(id=2,sample_interval_ms=300000,sessions=[newer,older])
                    status['recovery']={'available':True,'valid':True,'recipe_name':'Saved pale ale','step_name':'','current_step':-1,'step_seconds':0,'elapsed_seconds':125} if brewing else None
                    rows=[[i*15000,None,None,None,0,0,0,0,255,3,2,68.5+i,None,3.8,532.125+i] for i in range(3)]
                    def route_api(route):
                        request=route.request;path=request.url.split('/api/v1',1)[1];requests.append((request.method,path))
                        assert request.headers.get('authorization')=='Bearer '+KEY
                        code=200
                        if path=='': result=info
                        elif path=='/status':result=status
                        elif path=='/settings':result={'temperature_unit':'F','language':'en','detailed_status':False} if brewing else {'temperature_unit':'F'}
                        elif path=='/recipes':result={'capacity':16,'recipes':[]}
                        elif path=='/telemetry':result={'enabled':False,'host':'','port':8080,'status':{'state':'disabled'}}
                        elif path=='/commands':
                            body=request.post_data_json;commands.append(body);result={'accepted':True,'command':body['command']};code=202
                            if body['command'] in ('resume-interrupted','discard-interrupted'):status['recovery'].update(available=False,valid=True)
                        elif path=='/history':result=history
                        elif path.startswith('/history/samples/'):
                            offset=int(path.split('/samples/')[1].split('?')[0]);sample=rows[offset:offset+2]
                            requested_id=int(path.split('?id=')[1])
                            result={'id':requested_id,'offset':offset,'next_offset':offset+len(sample) if offset+len(sample)<len(rows) else None,'fields':FIELDS,'field_metadata':{'sensor_pressure_native':{'label':'Native pressure','unit':'raw','kind':'pressure'}},'samples':sample}
                        elif path.startswith('/display'):
                            offset=0 if path=='/display' else int(path.split('/')[2].split('?')[0]);assert offset==0 or path.endswith('?revision=7')
                            result={'width':128,'height':64,'rotation':0,'format':'row-gray4-msb-hex','revision':7,'total_bytes':4096,'offset':offset,'next_offset':offset+1024 if offset<3072 else None,'data':'0123456789abcdef'*128}
                        else:raise AssertionError('Unexpected API request '+path)
                        route.fulfill(status=code,content_type='application/json',body=json.dumps(result))
                    page.route('**/api/v1**',route_api)
                    page.goto(f'http://127.0.0.1:{server.server_port}/')
                    page.get_by_label('Device API key',exact=True).fill(KEY)
                    page.locator('#submitConnect').click()
                    expect(page.locator('#connectionText')).to_have_text('Connected')
                    assert commands==[], 'Connecting caused a mutation'
                    if brewing:
                        expect(page.locator('#displayHint')).to_have_text('LIVE OLED · 128 × 64')
                        pixels=page.locator('#display').evaluate('(canvas)=>Array.from(canvas.getContext("2d").getImageData(0,0,16,1).data)')
                        assert pixels[:4]==[18,38,32,255] and pixels[-4:]==[199,225,180,255]
                        assert len([path for _,path in requests if path.startswith('/display')])>=4
                        assert set(page.locator('#program option').all_text_contents())=={'Manual brewing','Clean','Drain','Chill'}
                        expect(page.get_by_role('button',name='Pause session',exact=True)).to_be_visible()
                        expect(page.get_by_role('button',name='Resume session',exact=True)).to_be_visible()
                        expect(page.get_by_role('button',name='Pause session',exact=True)).to_be_disabled()
                        expect(page.locator('#runProgram')).to_be_disabled()
                        expect(page.locator('#recoveryDetail')).to_have_text('Saved pale ale · Preparation')
                        expect(page.locator('#operationTitle')).to_have_text('An interrupted brew is waiting.')
                        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth+1'), 'Recovery panel overflow'
                        page.screenshot(path=str(output/f'recovery-{width}.png'),full_page=True)
                        dialogs=[]
                        def cancel_recovery(dialog):dialogs.append(dialog.message);dialog.dismiss()
                        page.once('dialog',cancel_recovery);page.locator('#resumeInterrupted').click()
                        assert commands==[] and all(word in dialogs[0] for word in ['water','vessel','filter','30 seconds','saved remaining'])
                        page.once('dialog',lambda dialog:dialog.accept());page.locator('#resumeInterrupted').click()
                        expect(page.locator('#recoveryPanel')).to_be_hidden()
                        assert commands[-1]=={'command':'resume-interrupted','confirm':True}
                        status['recovery'].update(available=False,valid=False)
                        expect(page.locator('#recoveryHeading')).to_have_text('The checkpoint could not be verified.')
                        expect(page.locator('#operationTitle')).to_have_text('The saved checkpoint needs attention.')
                        expect(page.locator('#resumeInterrupted')).to_be_hidden()
                        expect(page.locator('#runProgram')).to_be_disabled()
                        page.once('dialog',lambda dialog:dialog.dismiss());page.locator('#discardInterrupted').click();assert len(commands)==1
                        page.once('dialog',lambda dialog:dialog.accept());page.locator('#discardInterrupted').click()
                        expect(page.locator('#recoveryPanel')).to_be_hidden()
                        assert commands[-1]=={'command':'discard-interrupted'}
                        expect(page.locator('#runProgram')).to_be_enabled()
                        status['ready']=False
                        expect(page.locator('#runProgram')).to_be_disabled()
                        expect(page.locator('#stopButton')).to_be_enabled()
                        status['ready']=True
                    else:
                        expect(page.locator('#recoveryPanel')).to_be_hidden()
                        expect(page.locator('[data-tab="recipes"]')).to_be_hidden()
                        expect(page.locator('[data-feature="front_panel"]')).to_be_hidden()
                        expect(page.get_by_text('Thermoblock',exact=True)).to_be_hidden()
                        assert not any(path.startswith(('/display','/recipes')) for _,path in requests)
                        if model=='picoferm':
                            dialogs=[]
                            def cancel(dialog):dialogs.append(dialog.message);dialog.dismiss()
                            page.once('dialog',cancel);page.get_by_role('button',name='Calibrate pressure',exact=True).click();assert commands==[]
                            assert 'GRAY' in dialogs[0] and 'depressurized' in dialogs[0]
                            page.once('dialog',lambda dialog:dialog.accept());page.get_by_role('button',name='Calibrate pressure',exact=True).click()
                            expect(page.locator('#notice')).to_contain_text('accepted')
                            assert commands[-1]=={'command':'calibrate','confirm':True}
                            page.locator('[name="value"]').fill('900');page.get_by_role('button',name='Set sampling interval',exact=True).click()
                            expect(page.locator('#notice')).to_contain_text('accepted');assert commands[-1]=={'command':'set-sampling-interval','value':900}
                        else:
                            page.get_by_role('button',name='Set pump',exact=True).click();expect(page.locator('#notice')).to_contain_text('accepted');assert commands[-1]=={'command':'set-pump','enabled':False}
                            page.get_by_label('Fan channel',exact=True).fill('2');page.get_by_label('Fan level (%)',exact=True).fill('35');page.get_by_label('Fan enabled',exact=True).check();page.get_by_role('button',name='Set fan',exact=True).click()
                            expect(page.locator('#notice')).to_contain_text('accepted');assert commands[-1]=={'command':'set-fan','channel':2,'level':35,'enabled':True}
                    page.screenshot(path=str(output/f'{model}-{width}.png'),full_page=True)
                    assert page.evaluate('document.documentElement.scrollWidth<=innerWidth+1'), 'Horizontal page overflow'
                    page.locator('[data-tab="history"]').click()
                    expect(page.locator('#historySummary')).to_contain_text('3 readings')
                    page.locator('#historySession').select_option('1')
                    with page.expect_download() as download:page.locator('#historyExportJson').click()
                    exported=json.loads(Path(download.value.path()).read_text())
                    assert exported['metadata']['id']==1 and exported['metadata']['sample_interval_ms']==60000
                    page.locator('#historyMetric').select_option('sensor_pressure_native')
                    expect(page.locator('#historyReadout')).to_contain_text('Native pressure')
                    assert page.locator('#historyMetric option').all_text_contents().count('Native pressure')==1
                    assert 'sensor_pressure_native' in page.locator('#historyValues').text_content()
                    page.locator('[data-tab="settings"]').click()
                    if not brewing:expect(page.get_by_label('Front-panel language',exact=True)).to_be_hidden()
                    page.locator('[data-tab="api"]').click()
                    options=page.locator('#apiEndpoint option').all_text_contents()
                    assert any('/display/{offset}' in option for option in options)==brewing
                    if not brewing:assert not any('/recipes' in option or '/display' in option for option in options)
                    assert not errors, errors
                    checks.append({'model':model,'viewport':width,'requests':len(requests),'commands':commands,'errors':errors})
                    context.close()
            browser.close()
    finally:
        server.shutdown();server.server_close()
    (output/'results.json').write_text(json.dumps({'cases':checks,'hardware_contacted':False},indent=2)+'\n')
    print(f'{len(checks)} embedded website browser cases passed; results in {output}')

if __name__=='__main__':main()
