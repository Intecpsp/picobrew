'use strict';
(()=>{
const $=id=>document.getElementById(id), check=(ok,message)=>{if(!ok)throw Error(message);};
const names=['Application','index.html','app.js','api-explorer.js','history.js','style.css'];
const codes=['','index','app','explorer','history','style'], paths=['','i','a','e','h','s'];
const limits=[188416,7680,7680,3584,7680,3584], hash=/^[0-9a-f]{64}$/;
let key='',epoch=0,queue=Promise.resolve(),controller,inventory=null,packageData=null,planId=0,busy=false,mode='',stop=false;
const tell=(text,error=false)=>{$('message').textContent=text;$('message').classList.toggle('error',error);};
function matches(){return packageData && planId && inventory?.plan.active && inventory.plan.id===planId && inventory.plan.slot===packageData.plan.slot;}
function controls(){
  const ready=!!key && !!inventory, writable=ready && inventory.boot.recovery_selected && !inventory.boot_committed;
  const planned=writable && matches(), pending=!!inventory?.upload.active;
  $('connect').disabled=busy;$('key').disabled=busy;$('disconnect').disabled=!key;
  $('refresh').disabled=!ready || busy;
  for(const id of ['slot','binary','report','assets'])$(id).disabled=busy || (id==='slot' && !writable);
  $('plan').disabled=!writable || busy || pending || !$('binary').files.length || !$('report').files.length;
  $('upload').disabled=$('verify').disabled=!planned || busy || pending;
  const verified=planned && inventory.plan.verified_mask===inventory.plan.required_mask;
  $('trial').disabled=!verified || busy || pending || packageData.plan.slot!==2;
  $('select').disabled=!verified || busy || pending;
  $('abort').disabled=!ready || !pending || (busy && mode!=='upload');
}
function draw(){
  $('slots').replaceChildren();$('files').replaceChildren();
  if(inventory){
    const b=inventory.boot;
    $('boot').textContent='Recovery '+inventory.version+' · '+(!b.valid?'Unreadable boot record':b.recovery_selected?'Recovery selected':b.magic==='56788765'?'Image 2 trial selected':'Boot selector '+b.selector)+' · '+location.host;
    for(const slot of inventory.slots){
      const row=document.createElement('tr');row.dataset.slot=slot.slot;
      const label=!slot.present?'Unavailable (layout migration needed)':slot.verified?'Verified against plan':slot.vectors_valid===false?'Invalid vectors':'Not verified';
      for(const text of ['Image '+slot.slot,slot.present?slot.size+' / '+slot.allocated+' bytes':'—',label]){const cell=document.createElement('td');cell.textContent=text;row.append(cell);}
      $('slots').append(row);const option=$('slot').querySelector('[value="'+slot.slot+'"]');if(option)option.disabled=!slot.present;
    }
    if(matches())for(const file of inventory.plan.files){const li=document.createElement('li');li.textContent=names[file.target]+' · '+file.size+' bytes · '+((inventory.plan.verified_mask&(1<<file.target))?'Verified':'Pending verification');$('files').append(li);}
  }
  controls();
}
function api(suffix='',body){
  const current=epoch,credential=key;
  const result=queue.catch(()=>{}).then(async()=>{
    check(current===epoch && credential,'Connection changed.');controller=new AbortController();
    const timer=setTimeout(()=>controller.abort(),30000);
    try{
      const response=await fetch('/api/v1/recovery'+suffix,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+credential,'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body),cache:'no-store',credentials:'omit',redirect:'error',signal:controller.signal});
      const value=await response.json();check(current===epoch,'Connection changed.');
      check(response.ok,value.message || value.error || 'Request failed.');
      if(body!==undefined)check(value.ok===true,'Unrecognized response.');return value;
    }finally{clearTimeout(timer);controller=null;}
  });queue=result;return result;
}
async function refresh(){const value=await api();check(value.profile==='recovery' && Array.isArray(value.slots),'This is not the recovery manager.');inventory=value;draw();}
async function run(action,label=''){
  if(busy)return;const current=epoch;busy=true;mode=label;stop=false;controls();
  const send=(suffix,body)=>{check(current===epoch,'Connection changed.');return api(suffix,body);};
  try{await action(send);if(current===epoch && !inventory?.boot_committed)await refresh();}
  catch(error){if(current===epoch){
    if(stop && mode==='upload')try{await api('/abort',{id:planId});}catch(_){}
    if(current!==epoch)return;
    tell(error.name==='AbortError'?'Request timed out. Refresh before continuing.':error.message,true);
    try{await refresh();}catch(_){}
  }}finally{if(current===epoch){busy=false;mode='';controls();}}
}
function clearPlan(){packageData=null;planId=0;$('package').textContent='No package planned.';$('files').replaceChildren();controls();}
async function readPackage(){
  const file=$('binary').files[0],reportFile=$('report').files[0];
  check(file && reportFile && reportFile.size<=262144,'Choose a binary and firmware-report.json.');
  let r;try{r=JSON.parse(await reportFile.text());}catch(_){throw Error('Invalid firmware-report.json.');}
  check(r && ['standalone','legacy'].includes(r.profile) && r.sdk_link_passed===true && r.image_layout?.passed===true,'Unsupported or failed firmware report.');
  check(typeof r.firmware_version==='string' && /^[A-Za-z0-9][A-Za-z0-9._+-]{0,30}$/.test(r.firmware_version) && hash.test(r.binary_sha256),'Invalid version or binary hash.');
  const slot=Number($('slot').value), found=inventory.slots.find(item=>item.slot===slot);
  check(found?.present && file.size>=8 && file.size<=Math.min(limits[0],found.allocated) && file.size===r.image_layout.binary_bytes,'Binary size does not match the report or slot.');
  const head=new DataView(await file.slice(0,8).arrayBuffer()),sp=head.getUint32(0,true),reset=head.getUint32(4,true);
  check(Number(r.image_layout.load_address)===0x20004000 && sp===Number(r.image_layout.initial_stack_pointer) && reset===Number(r.image_layout.reset_vector) && !(sp&7) && sp>0x20004000 && sp<=0x20040000 && (reset&1) && reset>=0x20004001 && reset<0x20004000+file.size,'Binary vectors do not match the report.');
  const plan={slot,size:file.size,sha256:r.binary_sha256,version:r.firmware_version,web:!!r.browser_storage},files=[file];
  const supplied=[...$('assets').files];
  if(plan.web){
    const m=r.browser_storage;
    check(r.profile==='standalone' && m.schema_version===1 && m.block_size===4096 && m.metadata_per_file===512 && m.fail_safe===false && Array.isArray(m.assets) && m.assets.length===5 && supplied.length===5 && new Set(supplied.map(f=>f.name)).size===5,'Choose all five matching browser gzip files.');
    for(let i=1;i<6;i++){
      const entry=m.assets.find(item=>item.source===names[i]),asset=supplied.find(item=>item.name===names[i]+'.gz');
      check(entry && asset && entry.payload===asset.name && entry.file_path==='/local/web/'+paths[i]+'.gz' && entry.content_encoding==='gzip' && hash.test(entry.sha256) && Number.isSafeInteger(entry.length) && entry.length>=10 && entry.length<=limits[i] && entry.length===asset.size,'Browser file mapping, size or hash is invalid: '+names[i]);
      const blocks=Math.ceil((asset.size+512)/4096);
      check(entry.allocation_blocks===blocks && entry.max_size===blocks*4096-512 && entry.max_size<=limits[i],'Invalid browser allocation.');
      const header=new Uint8Array(await asset.slice(0,10).arrayBuffer());check(header[0]===31 && header[1]===139 && header[2]===8 && !(header[3]&224),'Invalid gzip file.');
      plan[codes[i]+'_size']=asset.size;plan[codes[i]+'_sha256']=entry.sha256;files[i]=asset;
    }
  }else check(!supplied.length,'This report does not use separate browser files.');
  return {plan,files,order:plan.web?[1,2,3,4,5,0]:[0]};
}
$('connectForm').addEventListener('submit',event=>{event.preventDefault();if(busy)return;const value=$('key').value.trim();if(!hash.test(value))return;
  epoch++;key=value;$('key').value='';inventory=null;clearPlan();run(async()=>{await refresh();tell('Connected. Choose a matching firmware package.');});
});
$('disconnect').onclick=()=>{epoch++;key='';controller?.abort();inventory=null;busy=false;mode='';clearPlan();draw();$('boot').textContent='Disconnected.';tell('Key removed. Reconnect to inspect or abort a pending upload.');};
$('refresh').onclick=()=>run(async()=>{await refresh();tell('Inventory refreshed.');});
for(const id of ['slot','binary','report','assets'])$(id).addEventListener('change',clearPlan);
$('plan').onclick=()=>run(async send=>{
  clearPlan();const candidate=await readPackage(),reply=await send('/plan',candidate.plan);
  check(Number.isSafeInteger(reply.id) && reply.id>0,'Missing plan ID.');packageData=candidate;planId=reply.id;
  $('package').textContent='Image '+candidate.plan.slot+' · firmware '+candidate.plan.version+' · '+candidate.plan.size+' bytes';tell('Plan accepted. Upload the package or verify installed files.');
});
$('verify').onclick=()=>run(async send=>{check(matches(),'Plan changed.');for(const target of packageData.order){tell('Checking '+names[target]+'…');const reply=await send('/verify',{id:planId,target});check(reply.id===planId && (reply.verified_mask&(1<<target)),'Readback did not verify.');}tell('All installed files verified. Boot selection is a separate step.');});
$('upload').onclick=()=>run(async send=>{
  check(matches(),'Plan changed.');const data=packageData,id=planId,total=data.files.reduce((sum,file)=>sum+file.size,0);let done=0;
  $('progress').hidden=false;$('progress').max=total;$('progress').value=0;
  for(const target of data.order){
    check(!stop,'Upload aborted.');const file=data.files[target];
    if(inventory.plan.verified_mask&(1<<target)){done+=file.size;continue;}
    const begin=await send('/begin',{id,target});check(begin.id===id && begin.next_offset===0,'Unexpected upload session.');inventory.upload={active:true,id,target};controls();
    for(let offset=0;offset<file.size;offset+=1024){
      check(!stop,'Upload aborted.');const bytes=new Uint8Array(await file.slice(offset,offset+1024).arrayBuffer());
      check(!stop,'Upload aborted.');const reply=await send('/chunk',{id,offset,data:Array.from(bytes,byte=>byte.toString(16).padStart(2,'0')).join('')});
      check(reply.id===id && reply.next_offset===offset+bytes.length,'Unexpected upload offset.');
      $('progress').value=done+reply.next_offset;tell('Writing '+names[target]+' · '+reply.next_offset+' / '+file.size+' bytes');
    }
    check(!stop,'Upload aborted.');tell('Reading back '+names[target]+'…');const reply=await send('/finish',{id});
    check(reply.id===id && (reply.verified_mask&(1<<target)),'Readback did not verify.');inventory.plan.verified_mask=reply.verified_mask;inventory.upload.active=false;done+=file.size;draw();
  }
  $('progress').value=total;tell('Package uploaded and verified. Choose the next boot separately.');
},'upload');
$('abort').onclick=()=>{if(mode==='upload'){stop=true;tell('Stopping after the current request…');return;}run(async send=>{await send('/abort',{id:inventory.upload.id});tell('Upload aborted. Recovery remains selected.');});};
function select(trial){
  if(!matches() || !confirm(trial?'Restart into image 2 once now? The following reset returns to recovery.':'Set image '+packageData.plan.slot+' as the default and restart now? Returning to recovery currently requires USB boot-metadata programming. Use a trial for the first test.'))return;
  run(async send=>{const reply=await send('/select',{id:planId,trial});check(reply.id===planId && reply.boot_committed,'Boot selection was not confirmed.');inventory.boot_committed=true;controls();tell('Boot selection verified. The Pico is restarting.');});
}
$('trial').onclick=()=>select(true);$('select').onclick=()=>select(false);draw();
})();
