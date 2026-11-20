'use strict';

const $ = id => document.getElementById(id);
const state = {key:'', connected:false, generation:0, status:null, recipes:[],
  settings:{temperature_unit:'F', language:'en', detailed_status:false}, capabilities:[],
  editorId:null, dirty:false, tab:'overview', polling:false, restoreDefaults:false,
  telemetry:null, telemetryAt:0, info:{}, pending:false};
let requestQueue = Promise.resolve();
let pollTimer;
let apiExplorer;
let brewHistory;
const hasFeature = name => PicoDevice.feature(state.info,name);
const allowedCommand = name => state.capabilities.includes(name);
const programLabels = {'sous-vide':'Sous vide', 'cold-brew':'Cold brew', manual:'Manual brewing',
  rinse:'Rinse', 'first-rinse':'First rinse', descale:'Descale', circulate:'Circulate',
  'drain-reservoir':'Drain reservoir', 'drain-filter':'Drain step filter', 'rack-beer':'Rack beer',
  clean:'Clean',drain:'Drain',chill:'Chill',utilities:'Utilities menu', settings:'Settings menu'};

function notify(message, error=false) {
  $('notice').textContent = message;
  $('notice').classList.toggle('error', error);
  $('notice').hidden = !message;
}

function api(path, method='GET', body) {
  const generation = state.generation;
  const key = state.key;
  const task = async () => {
    if (generation !== state.generation) throw new Error('Connection changed.');
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 6000);
    try {
      const headers = {Accept:'application/json', Authorization:'Bearer '+key};
      if (body !== undefined || ['POST','PUT','PATCH'].includes(method)) headers['Content-Type'] = 'application/json';
      const response = await fetch('/api/v1'+path, {method, headers,
        body:body === undefined ? undefined : JSON.stringify(body),
        signal:controller.signal, cache:'no-store', credentials:'omit', redirect:'error'});
      let data;
      try { data = response.status === 204 ? null : await response.json(); }
      catch (_) { throw new Error('The device returned an unreadable response.'); }
      if (!response.ok) {
        const error = new Error(data.message || data.error || 'Request failed ('+response.status+').');
        error.status = response.status;
        error.code = data.error;
        throw error;
      }
      if (generation !== state.generation) throw new Error('Connection changed.');
      return data;
    } catch (error) {
      if (error.name === 'AbortError') throw new Error('The device did not respond. Check its connection.');
      throw error;
    } finally { clearTimeout(timeout); }
  };
  // One connection at a time matches the Pico's bounded embedded HTTP server.
  const result = requestQueue.catch(() => {}).then(task);
  requestQueue = result;
  return result;
}

function connected(value) {
  state.connected = value;
  $('connectionText').textContent = value ? 'Connected' : 'Disconnected';
  $('connectionDot').classList.toggle('online', value);
  $('connectButton').textContent = value ? 'Device key' : 'Connect';
  updateControls();
}

function updateControls() {
  const ready = state.connected && state.status?.ready === true && !state.pending;
  const busy = !!state.status?.busy;
  const stopped = !!state.status?.stopped;
  const recovery=state.status?.recovery;
  const interrupted=!!recovery && (recovery.available===true || recovery.valid===false);
  document.querySelectorAll('[data-encoder]').forEach(button => button.disabled = !ready);
  $('encoderClick').disabled = !ready;
  $('brewRecipe').disabled = !ready || busy || stopped || interrupted;
  $('startRecipe').disabled = !ready || busy || stopped || interrupted || !$('brewRecipe').value;
  $('program').disabled = !ready || busy || stopped || interrupted;
  $('runProgram').disabled = !ready || busy || stopped || interrupted || !$('program').value;
  $('stopButton').disabled = !state.connected || state.pending || !allowedCommand('stop');
  $('clearStop').hidden = !stopped;
  $('clearStop').disabled = !ready || busy;
  $('sessionButtons').hidden=!allowedCommand('pause')&&!allowedCommand('resume');
  for(const command of ['pause','resume']) {
    $(command+'Session').hidden=!allowedCommand(command);
    $(command+'Session').disabled=!ready || !busy || stopped;
  }
  $('recoveryPanel').hidden=!state.connected || !interrupted;
  $('resumeInterrupted').hidden=!allowedCommand('resume-interrupted') || recovery?.valid!==true;
  $('resumeInterrupted').disabled=!state.connected || state.pending || busy || stopped || recovery?.available!==true || recovery?.valid!==true;
  $('discardInterrupted').hidden=!allowedCommand('discard-interrupted');
  $('discardInterrupted').disabled=!state.connected || state.pending || busy || !interrupted;
  $('newRecipe').disabled = !ready || busy;
  $('restoreRecipes').disabled = !ready || busy || !state.restoreDefaults;
  $('saveRecipe').disabled = !ready || busy;
  $('deleteRecipe').disabled = !ready || busy || !state.editorId;
  $('saveSettings').disabled = !ready || busy;
  document.querySelectorAll('[data-action]').forEach(button=>button.disabled=!ready || !allowedCommand(button.dataset.action) || (busy && ['start','calibrate','clean','essential-oils'].includes(button.dataset.action)));
  $('accessoryStop').disabled=!state.connected || state.pending || !allowedCommand('stop');
  apiExplorer?.update();
  brewHistory?.update();
}

function temperature(value) {
  const number = Number(value);
  if (value === null || value === undefined || !Number.isFinite(number)) return '—';
  const unit = state.settings.temperature_unit;
  return (unit === 'C' ? (number-32)*5/9 : number).toFixed(1)+'°'+unit;
}

function duration(seconds) {
  if (!Number.isFinite(Number(seconds))) return '—';
  const value = Math.max(0, Math.floor(Number(seconds)));
  const minutes = Math.floor(value/60);
  return minutes+':'+String(value%60).padStart(2,'0');
}

function renderIdentity(info) {
  $('modelName').textContent=PicoDevice.label(info)+' console';
  document.title=PicoDevice.label(info)+' · Pico Local';
}
function applyCapabilities(info) {
  state.info=info;
  document.body.classList.toggle("accessory",!hasFeature("brewing"));
  $("recipeRoutingHeader").textContent=info.model==="z-series"?"Direct valve":"Location";
  $("recipeRoutingHint").textContent=info.model==="z-series"?"Z uses direct valve locations 1–6. Archived Z/Zymatic location and pause codes must be explicitly converted before importing a program.":"Use verified locations 1–6 for this device.";
  document.querySelectorAll('[data-feature]').forEach(node=>node.hidden=!hasFeature(node.dataset.feature));
  document.querySelectorAll('[data-accessory]').forEach(node=>node.hidden=hasFeature('brewing') ||
    (node.dataset.feature && !hasFeature(node.dataset.feature)));
  const fields=Array.isArray(info.settings_fields)?info.settings_fields:['temperature_unit','language','detailed_status'];
  document.querySelectorAll('[data-setting]').forEach(node=>node.hidden=!fields.includes(node.dataset.setting));
  $('temperatureUnit').closest('.settings-row').hidden=!fields.includes('temperature_unit');
  $('settingsForm').hidden=!fields.length;
  $('restoreRecipes').hidden=info.restore_default_recipes!==true;
  if(state.tab==='recipes'&&!hasFeature('recipes'))selectTab('overview');
  renderActions();apiExplorer?.configure(info);
}
function renderActions() {
  const actions=$('deviceActions'),parameters=$('parameterControls');actions.replaceChildren();parameters.replaceChildren();
  const labels={...PicoDevice.commandLabels,start:hasFeature('fermentation')?'Start fermentation':'Start manual session'};
  $('controlsHeading').textContent=hasFeature('fermentation')?'Monitor fermentation.':'Run your PicoStill.';
  $('controlHint').textContent=hasFeature('fermentation')?'Readings and session history are recorded on the device.':
    'Prepare the device before starting and review its live sensor and output readings.';
  for(const name of state.capabilities) {
    if(name==='stop'||!labels[name])continue;
    const descriptor=state.info.command_fields?.[name] || {allowed:[],required:[]};
    if(['set-pump','set-fan','set-sampling-interval'].includes(name)) {
      const form=document.createElement('form');form.className='control-form';form.dataset.command=name;
      const heading=document.createElement('h3');heading.textContent=labels[name];form.append(heading);
      const add=(field,label,type,minimum,maximum,value)=>{
        const wrapper=document.createElement('label'),input=document.createElement('input');wrapper.textContent=label;
        input.name=field;input.type=type;input.setAttribute('aria-label',label);input.required=type!=='checkbox';
        if(type==='checkbox')input.checked=!!value;else{input.min=minimum;input.max=maximum;input.step='1';input.value=value;}
        wrapper.append(input);form.append(wrapper);return input;
      };
      if(name==='set-fan') {add('channel','Fan channel','number',1,2,1);add('level','Fan level (%)','number',0,100,0);add('enabled','Fan enabled','checkbox',0,0,false);}
      else if(name==='set-pump')add('enabled','Pump enabled','checkbox',0,0,false);
      else add('value','Sampling interval (seconds)','number',60,86400,300);
      const button=document.createElement('button');button.type='submit';button.className='button';button.dataset.action=name;button.textContent=labels[name];form.append(button);
      form.addEventListener('submit',event=>{event.preventDefault();if(!form.reportValidity())return;
        const body={command:name};form.querySelectorAll('input').forEach(input=>body[input.name]=input.type==='checkbox'?input.checked:Number(input.value));sendCommand(body);});
      parameters.append(form);
    } else {
      const button=document.createElement('button');button.type='button';button.className='button primary';button.dataset.action=name;button.textContent=labels[name];
      button.addEventListener('click',()=>{
        const body={command:name};
        if(name==='calibrate') {
          if(!confirm('Remove PicoFerm from any keg or keg adapter. Remove the GRAY fermentation seal and make sure the device is fully depressurized. Start pressure calibration?'))return;
          body.confirm=true;
        } else if(descriptor.required.includes('confirm')) {
          if(!confirm('Confirm the device is prepared for '+labels[name].toLowerCase()+'.'))return;
          body.confirm=true;
        }
        sendCommand(body);
      });actions.append(button);
    }
  }
  $('accessoryStopRow').hidden=!allowedCommand('stop');updateControls();
}
function renderReadings(status) {
  const render=(target,items,actuator)=>{
    const container=$(target);container.replaceChildren();
    for(const item of items) {
      const card=document.createElement('article');card.className='metric';const label=document.createElement('span'),reading=document.createElement('strong'),hint=document.createElement('small');
      label.textContent=item.label || item.id;
      if(actuator) {
        reading.textContent=item.enabled==null?'—':item.enabled?(PicoDevice.finite(item.level)?item.level.toFixed(0)+'%':'On'):'Off';
        hint.textContent=PicoDevice.finite(item.rpm)?item.rpm.toFixed(0)+' rpm':item.kind;
      } else {
        reading.textContent=item.unit==='F'?temperature(item.value):PicoDevice.finite(item.value)?item.value.toFixed(item.unit==='raw'?0:2)+(item.unit==='raw'?'': ' '+item.unit):'—';
        hint.textContent=item.unit==='raw'?'Raw device reading':item.kind;
      }
      card.append(label,reading,hint);container.append(card);
    }
  };
  const sensors=Array.isArray(status.sensors)?status.sensors.slice(0,8):[],actuators=Array.isArray(status.actuators)?status.actuators.slice(0,4):[];
  render('sensorReadings',sensors,false);render('actuatorReadings',actuators,true);
  for(const id of ['sensorTemperature','sensorPressure','sensorBattery'])if(sensors.length)$(id).closest('.metric').hidden=true;
  for(const id of ['pumpOutput','fanOutput'])if(actuators.length)$(id).closest('.metric').hidden=true;
}

function renderStatus(status) {
  state.status = status;
  if (status.model) renderIdentity(status);
  renderReadings(status);
  const recovery=status.recovery;
  $('recoveryHeading').textContent=recovery?.valid===false?'The checkpoint could not be verified.':'Review the saved checkpoint.';
  $('recoveryDetail').textContent=recovery?.valid===false?'Discard the invalid checkpoint before starting another recipe.':
    recovery?.available?(recovery.recipe_name+' · '+(recovery.step_name || 'Preparation')):'';
  $('recoveryTime').textContent=recovery?.available && recovery.valid===true?
    (recovery.step_seconds>0?duration(recovery.step_seconds)+' remaining in the saved step · ':'Preparation or untimed phase · ')+duration(recovery.elapsed_seconds)+' saved elapsed time':'';
  $('sensorTemperature').textContent=temperature(status.temperature_f);
  $('sensorPressure').textContent=PicoDevice.finite(status.pressure_kpa)?status.pressure_kpa.toFixed(2)+' kPa':'—';
  $('sensorBattery').textContent=PicoDevice.finite(status.battery_v)?status.battery_v.toFixed(2)+' V':'—';
  $('wortTemperature').textContent = temperature(status.wort_f);
  $('blockTemperature').textContent = temperature(status.thermoblock_f);
  $('targetTemperature').textContent = status.target_f === 0 ? 'Unheated' : temperature(status.target_f);
  $('stepTime').textContent = duration(status.step_seconds);
  const phases = ['Heating','Holding temperature','Cooling','Draining','Idle','Circulating','Cooling thermoblock','Pump hold','Heating'];
  $('phaseName').textContent = phases[status.phase] || 'Phase '+status.phase;
  $('operationTitle').textContent = status.stopped ? 'Outputs stopped.' : status.busy ? (status.recipe_name || 'Operation in progress.') : 'Ready for your next brew.';
  $('operationDetail').textContent = status.stopped ? (status.stop_reason === 'filter-removed' ? 'The step filter was removed during pumping. This operation is cancelled. Refit the filter and wait for idle before clearing the stop.' : 'The stop latch is active. Wait for the operation to end before clearing it.') : status.busy ? (status.step_name || 'Follow the brewer’s prompts on the remote front panel.') : 'Choose a saved recipe, open a program, or use the front panel.';
  $('modeBadge').textContent = status.stopped ? 'STOPPED' : status.busy ? 'IN PROGRESS' : 'READY';
  $('modeBadge').classList.toggle('stopped', !!status.stopped);
  const reviewRecovery=!!recovery && (recovery.available===true || recovery.valid===false) && !status.busy && !status.stopped;
  $('modeBadge').classList.toggle('warning',reviewRecovery);
  if(reviewRecovery){
    $('operationTitle').textContent=recovery.valid?'An interrupted brew is waiting.':'The saved checkpoint needs attention.';
    $('operationDetail').textContent=recovery.valid?'Review its checkpoint before starting another program.':'Discard the invalid checkpoint before starting another recipe.';
    $('modeBadge').textContent='REVIEW CHECKPOINT';
  }
  $('filterBadge').textContent = status.filter_inserted === null ? 'FILTER UNKNOWN' : status.filter_inserted ? 'FILTER INSERTED' : 'FILTER REMOVED';
  $('filterBadge').classList.toggle('warning', status.filter_inserted === false);
  if(!hasFeature('brewing')) {
    $('operationTitle').textContent=status.stopped?'Operation stopped.':status.state?.replace(/[_-]/g,' ').replace(/^./,letter=>letter.toUpperCase()) || (status.busy?'Session in progress.':'Ready.');
    $('operationDetail').textContent=status.stop_reason && status.stop_reason!=='none'?'Stop reason: '+status.stop_reason:status.busy?'The device is running this session and recording its readings.':'Choose an available device control below.';
  }
  $('filterBadge').hidden=status.filter_inserted==null;
  $('deviceId').textContent = status.device_uid ? 'Device '+status.device_uid : '';
  $('lastUpdated').textContent = 'Updated '+new Date().toLocaleTimeString();
  updateControls();
}

function renderDisplay(frame) {
  const pixels=PicoDevice.decodeDisplay(frame),canvas=$('display');canvas.width=frame.width;canvas.height=frame.height;
  const context=canvas.getContext('2d'),image=context.createImageData(frame.width,frame.height);
  pixels.forEach((brightness,index)=>{
    const light=brightness/255;image.data[index*4]=Math.round(18+181*light);image.data[index*4+1]=Math.round(38+187*light);image.data[index*4+2]=Math.round(32+148*light);image.data[index*4+3]=255;
  });context.putImageData(image,0,0);$('displayHint').textContent='LIVE OLED · '+frame.width+' × '+frame.height;
}

function renderPrograms() {
  $('program').replaceChildren();
  for (const command of state.capabilities) if (programLabels[command]) {
    $('program').add(new Option(programLabels[command],command));
  }
  updateControls();
}

async function refreshRecipes() {
  const data = await api('/recipes');
  state.recipes = data.recipes || [];
  const previous = $('brewRecipe').value;
  $('brewRecipe').replaceChildren(new Option('Choose a recipe',''));
  $('recipeList').replaceChildren();
  for (const recipe of state.recipes) {
    $('brewRecipe').add(new Option(recipe.name,recipe.id));
    const button=document.createElement('button'); button.type='button'; button.className='recipe-item';
    button.classList.toggle('active', recipe.id === state.editorId);
    const name=document.createElement('strong');name.textContent=recipe.name;
    const detail=document.createElement('small');detail.textContent=recipe.steps+' steps · '+recipe.id;
    button.append(name,detail);button.addEventListener('click',()=>loadRecipe(recipe.id));$('recipeList').append(button);
  }
  if (!state.recipes.length) {
    const empty=document.createElement('p');empty.className='empty';empty.textContent='Your collection is empty. Create or import your first recipe.';$('recipeList').append(empty);
  }
  if (state.recipes.some(recipe=>recipe.id===previous)) $('brewRecipe').value=previous;
  else if (state.recipes.length) $('brewRecipe').value=state.recipes[0].id;
  $('recipeCount').textContent=state.recipes.length;
  $('recipeCapacity').textContent=state.recipes.length+' / '+data.capacity;
  updateControls();
}

function addStep(step={name:'New step',location:1,target_f:152,minutes:20,drain_minutes:0}) {
  if ($('steps').children.length>=15) {notify('A recipe can contain up to 15 steps.',true);return;}
  const row=document.createElement('tr');
  const definitions=[['name','text',null,null],['location','select',1,6],['target_f','number',0,212],['minutes','number',0,599],['drain_minutes','number',0,120]];
  for (const [field,type,min,max] of definitions) {
    const cell=document.createElement('td');let input;
    if(type==='select') {input=document.createElement('select');for(let i=1;i<=6;i++)input.add(new Option(String(i),String(i)));}
    else {input=document.createElement('input');input.type=type;if(type==='text')input.maxLength=21;else{input.min=min;input.max=max;input.step=1;}}
    input.dataset.field=field;input.required=true;input.value=step[field] ?? '';
    input.setAttribute('aria-label',field.replaceAll('_',' '));cell.append(input);row.append(cell);
  }
  const cell=document.createElement('td');const remove=document.createElement('button');remove.type='button';remove.className='remove';remove.textContent='×';remove.setAttribute('aria-label','Remove step');
  remove.addEventListener('click',()=>{row.remove();state.dirty=true;});cell.append(remove);row.append(cell);$('steps').append(row);
}

function editRecipe(recipe, existing=false) {
  state.editorId=existing ? recipe.id : null;state.dirty=false;
  $('recipeName').value=recipe.name || '';$('recipeId').value=recipe.id || '';$('recipeId').readOnly=existing;
  $('recipeId').dataset.auto=existing ? 'false' : 'true';
  $('editorHeading').textContent=existing ? 'Edit recipe' : 'New recipe';$('steps').replaceChildren();
  for(const step of recipe.steps || [{name:'Mash',location:1,target_f:152,minutes:60,drain_minutes:0}])addStep(step);
  updateControls();
}

async function loadRecipe(id) {
  if(state.dirty && !confirm('Discard the unsaved recipe changes?'))return;
  try {const recipe=await api('/recipes/'+encodeURIComponent(id));editRecipe({...recipe,id},true);notify('');}
  catch(error){notify(error.message,true);}
}

function recipeFromForm() {
  if(!$('recipeForm').reportValidity())return null;
  const steps=Array.from($('steps').children).map(row=>{
    const step={};row.querySelectorAll('[data-field]').forEach(input=>step[input.dataset.field]=input.dataset.field==='name'?input.value:Number(input.value));return step;
  });
  if(!steps.length){notify('Add at least one brewing step.',true);return null;}
  if(steps.some(step=>step.target_f!==0 && step.target_f<60)){notify('Targets must be 0 for unheated circulation, or between 60 and 212 °F.',true);return null;}
  return {id:$('recipeId').value,name:$('recipeName').value,steps};
}

async function sendCommand(body) {
  if(state.pending || !state.connected || !allowedCommand(body.command))return;
  state.pending=true;updateControls();
  try {
    const reply=await api('/commands','POST',body);
    if(!reply?.accepted || reply.command!==body.command)throw new Error('Command acceptance was not confirmed.');
    notify(body.command==='stop'?'Stop requested.':'Command accepted.');renderStatus(await api('/status'));
  } catch(error){notify(error.message+(!error.status?' Check the device status before repeating the command.':''),true);}
  finally{state.pending=false;updateControls();}
}

function download(name,data) {
  const blob=new Blob([JSON.stringify(data,null,2)+'\n'],{type:'application/json'});
  const url=URL.createObjectURL(blob);const link=document.createElement('a');link.href=url;link.download=name;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}

function renderSettings(settings) {
  state.settings={temperature_unit:'F',...settings};$('temperatureUnit').value=state.settings.temperature_unit;$('language').value=settings.language || 'en';$('detailedStatus').checked=!!settings.detailed_status;
}

function renderTelemetry(config) {
  state.telemetry=config;
  $('hubDestination').textContent=config.enabled && config.host ?
    'Sending live readings to '+config.host+':'+config.port+'.' : 'No Hub destination configured.';
  const status=config.status || {};
  const labels={disabled:'Add this device in PicoBrew Hub to connect automatically.',offline:'Waiting for the Pico’s network connection.',
    'waiting-idle':'Waiting for the device to be idle to connect.',ready:'Ready to send readings.',
    connecting:'Connecting…',sending:'Sending readings…',receiving:'Waiting for acknowledgement…',
    backoff:'Destination unavailable. The Pico will retry.'};
  $('telemetryStatus').textContent=(labels[status.state] || 'Waiting for connection status.')+
    ' Sent: '+(status.sent || 0)+' · Failed: '+(status.failed || 0)+' · Skipped: '+(status.dropped || 0)+
    (status.last_http_status?' · Last reply: HTTP '+status.last_http_status:'')+
    (status.storage_error?' · Destination settings could not be saved.':'');
  updateControls();
}

async function refreshTelemetry() {
  const generation=state.generation;
  state.telemetryAt=Date.now();
  try {renderTelemetry(await api('/telemetry'));}
  catch(error) {if(generation===state.generation)$('telemetryStatus').textContent=error.message;}
}

async function poll() {
  clearTimeout(pollTimer);
  if(!state.key || state.polling)return;
  state.polling=true;const generation=state.generation;
  try {
    if(!document.hidden) {
      renderStatus(await api('/status'));connected(true);
      if(state.tab==='overview' && hasFeature('front_panel')) {try{renderDisplay(await PicoDevice.readDisplay(api));}catch(error){$('displayHint').textContent=error.message;}}
      if(state.tab==='settings' && state.info.telemetry_forwarding && Date.now()-state.telemetryAt>=5000)await refreshTelemetry();
    }
  } catch(error) {
    if(generation===state.generation){connected(false);notify(error.message,true);if(error.status===401){state.key='';try{sessionStorage.removeItem('pico-api-key');}catch(_){}}}
  } finally {state.polling=false;if(state.key)pollTimer=setTimeout(poll,state.connected?1200:3000);}
}

function selectTab(tab) {
  if(tab==='recipes'&&!hasFeature('recipes'))return;
  state.tab=tab;document.querySelectorAll('.tab-panel').forEach(panel=>panel.hidden=panel.id!==tab);
  document.querySelectorAll('[data-tab]').forEach(button=>{const active=button.dataset.tab===tab;button.classList.toggle('selected',active);if(active)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');});
  if(state.connected && tab==='recipes')refreshRecipes().catch(error=>notify(error.message,true));
  if(state.connected && tab==='settings' && state.info.telemetry_forwarding)refreshTelemetry();
  if(tab==='history')brewHistory?.activate();
}

async function connect(key) {
  state.key=key;state.generation++;clearTimeout(pollTimer);
  state.telemetry=null;state.telemetryAt=0;
  const info=await api('');state.capabilities=PicoDevice.commands(info);applyCapabilities(info);renderPrograms();
  renderIdentity(info);
  state.restoreDefaults=info.restore_default_recipes === true;
  $('firmwareVersion').textContent=info.firmware_version ? 'Firmware '+info.firmware_version : 'Standalone firmware';
  renderSettings(await api('/settings'));renderStatus(await api('/status'));if(hasFeature('recipes'))await refreshRecipes();
  try{sessionStorage.setItem('pico-api-key',key);}catch(_){}
  connected(true);notify('');$('connectDialog').close();$('apiKey').value='';poll();
}

function disconnect() {
  state.generation++;state.key='';state.status=null;clearTimeout(pollTimer);connected(false);
  state.telemetry=null;$('telemetryStatus').textContent='Connect to see Hub connection status.';
  $('hubDestination').textContent='No Hub connection loaded.';
  try{sessionStorage.removeItem('pico-api-key');}catch(_){}
  $('operationTitle').textContent='Disconnected from your device.';$('operationDetail').textContent='A running session continues on the device. Reconnect to see its status.';
  $('apiKey').value='';notify('Device key removed from this tab.');
}

document.querySelectorAll('[data-tab]').forEach(button=>button.addEventListener('click',()=>selectTab(button.dataset.tab)));
document.querySelectorAll('[data-encoder]').forEach(button=>button.addEventListener('click',()=>sendCommand({command:'encoder',delta:Number(button.dataset.encoder),click:false})));
$('encoderClick').addEventListener('click',()=>sendCommand({command:'encoder',delta:0,click:true}));
$('brewRecipe').addEventListener('change',updateControls);
$('startRecipe').addEventListener('click',()=>sendCommand({command:'brew',recipe_id:$('brewRecipe').value}));
$('runProgram').addEventListener('click',()=>sendCommand({command:$('program').value}));
$('stopButton').addEventListener('click',()=>sendCommand({command:'stop'}));
$('accessoryStop').addEventListener('click',()=>sendCommand({command:'stop'}));
$('clearStop').addEventListener('click',()=>sendCommand({command:'clear-stop'}));
$('pauseSession').addEventListener('click',()=>sendCommand({command:'pause'}));
$('resumeSession').addEventListener('click',()=>sendCommand({command:'resume'}));
$('resumeInterrupted').addEventListener('click',()=>{
  const recovery=state.status?.recovery;
  if(!recovery?.available || recovery.valid!==true)return;
  if(confirm('Verify the water, vessel and filter before resuming. Saved progress can be up to 30 seconds behind the interruption. The brewer resumes with its saved remaining step time and may return to preparation prompts. Resume '+recovery.recipe_name+'?'))
    sendCommand({command:'resume-interrupted',confirm:true});
});
$('discardInterrupted').addEventListener('click',()=>{
  if(confirm('Discard the interrupted-brew checkpoint? This saved operation cannot be resumed afterward.'))sendCommand({command:'discard-interrupted'});
});
$('connectButton').addEventListener('click',()=>{$('connectError').textContent='';$('connectDialog').showModal();});
$('cancelConnect').addEventListener('click',()=>$('connectDialog').close());
$('disconnectButton').addEventListener('click',disconnect);
$('connectForm').addEventListener('submit',async event=>{
  event.preventDefault();$('submitConnect').disabled=true;$('connectError').textContent='';
  try{await connect($('apiKey').value.trim());}catch(error){connected(false);$('connectError').textContent=error.message;}finally{$('submitConnect').disabled=false;}
});
$('newRecipe').addEventListener('click',()=>{if(!state.dirty || confirm('Discard the unsaved recipe changes?')){editRecipe({});$('recipeName').focus();}});
$('restoreRecipes').addEventListener('click',async()=>{
  if(!confirm('Add missing starter recipes? Recipes already on the Pico, including your edits, will be preserved.'))return;
  $('restoreRecipes').disabled=true;
  try{const result=await api('/recipes/restore-defaults','POST');await refreshRecipes();notify(result.added ? result.added+' starter recipes added. Existing recipes preserved.' : 'All starter recipes are already on the Pico.');}
  catch(error){notify(error.message,true);}finally{updateControls();}
});
$('addStep').addEventListener('click',()=>{addStep();state.dirty=true;});
$('recipeForm').addEventListener('input',()=>{state.dirty=true;});
$('recipeName').addEventListener('input',()=>{if($('recipeId').dataset.auto==='true')$('recipeId').value=$('recipeName').value.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'').slice(0,20);});
$('recipeId').addEventListener('input',()=>{$('recipeId').dataset.auto='false';});
$('recipeForm').addEventListener('submit',async event=>{
  event.preventDefault();const recipe=recipeFromForm();if(!recipe)return;
  try{await api('/recipes/'+encodeURIComponent(recipe.id),'PUT',{name:recipe.name,steps:recipe.steps});editRecipe(recipe,true);await refreshRecipes();notify('Recipe saved on the Pico.');}
  catch(error){notify(error.message,true);}
});
$('deleteRecipe').addEventListener('click',async()=>{
  if(!state.editorId || !confirm('Delete this recipe from the Pico?'))return;
  try{await api('/recipes/'+encodeURIComponent(state.editorId),'DELETE');editRecipe({});await refreshRecipes();notify('Recipe deleted.');}
  catch(error){notify(error.message,true);}
});
$('importRecipe').addEventListener('click',()=>$('importFile').click());
$('importFile').addEventListener('change',async event=>{
  const file=event.target.files[0];event.target.value='';if(!file)return;
  try{
    if(file.size>16384)throw new Error('Recipe file is too large.');
    const recipe=JSON.parse(await file.text());
    if(!recipe || !Array.isArray(recipe.steps) || recipe.steps.length<1 || recipe.steps.length>15 || typeof recipe.name!=='string' || typeof recipe.id!=='string' || !/^[A-Za-z0-9_-]{1,20}$/.test(recipe.id) || recipe.steps.some(step=>!step || typeof step!=='object'))throw new Error('Choose a recipe JSON file exported by Pico Local.');
    if(state.dirty && !confirm('Discard the unsaved recipe changes?'))return;
    editRecipe(recipe,false);state.dirty=true;notify('Recipe imported into the editor. Review it, then save to the Pico.');
  }catch(error){notify(error instanceof SyntaxError?'The file is not valid JSON.':error.message,true);}
});
$('exportRecipe').addEventListener('click',()=>{const recipe=recipeFromForm();if(recipe)download(recipe.id+'.json',recipe);});
$('settingsForm').addEventListener('submit',async event=>{
  event.preventDefault();try{const available={temperature_unit:$('temperatureUnit').value,language:$('language').value,detailed_status:$('detailedStatus').checked};const keys=state.info.settings_fields || Object.keys(available);const patch=Object.fromEntries(keys.filter(key=>key in available).map(key=>[key,available[key]]));const settings=await api('/settings','PATCH',patch);renderSettings(settings);if(state.status)renderStatus(state.status);notify('Preferences saved on the Pico.');}catch(error){notify(error.message,true);}
});
document.addEventListener('visibilitychange',()=>{if(!document.hidden && state.key)poll();});
apiExplorer=setupApiExplorer({session:()=>({key:state.key,connected:state.connected,generation:state.generation}),enqueue:task=>{
  const result=requestQueue.catch(()=>{}).then(task);requestQueue=result;return result;
}});
brewHistory=setupHistory({session:()=>({key:state.key,connected:state.connected,generation:state.generation,temperature_unit:state.settings.temperature_unit,brewing:hasFeature('brewing')}),enqueue:task=>{
  const result=requestQueue.catch(()=>{}).then(task);requestQueue=result;return result;
}});
editRecipe({});
try{state.key=sessionStorage.getItem('pico-api-key') || '';}catch(_){}
if(/^[0-9a-f]{64}$/.test(state.key))connect(state.key).catch(error=>{connected(false);notify(error.message,true);$('connectDialog').showModal();});
else $('connectDialog').showModal();
