const { test } = require('node:test');
const assert = require('node:assert/strict');
const device = require('../web/device.js');

test('nested capability flags govern controls without inferring accessory hardware', () => {
  for (const model of ['picoferm', 'picostill']) {
    const info = {model, recipe_capacity:16, front_panel:true, capabilities:{recipes:false,front_panel:false,brewing:false,temperature:true}};
    assert.equal(device.feature(info,'recipes'),false);
    assert.equal(device.feature(info,'front_panel'),false);
    assert.equal(device.feature(info,'brewing'),false);
    assert.equal(device.feature(info,'temperature'),true);
  }
  assert.equal(device.feature({recipe_capacity:16,front_panel:true},'brewing'),true);
});

test('grayscale decoder preserves all sixteen levels, nibble order and row order', () => {
  const hex='0123456789abcdef'+'00'.repeat(4096-8);
  const frame={width:128,height:64,rotation:0,format:'row-gray4-msb-hex',data:hex};
  const pixels=device.decodeDisplay(frame);
  assert.deepEqual([...pixels.slice(0,16)],Array.from({length:16},(_,i)=>i*17));
  assert.equal(pixels.length,8192);assert.equal(pixels[128],0);
  for(const invalid of [{height:32},{rotation:undefined},{data:hex.slice(2)},{data:'zz'+hex.slice(2)}])
    assert.throws(()=>device.decodeDisplay({...frame,...invalid}));
});

test('legacy mono and C logical viewport retain exact orientation', () => {
  const pixels=device.decodeDisplay({width:128,height:64,format:'page-lsb-hex',data:'01'+'00'.repeat(1023)});
  assert.equal(pixels[8191],255);assert.equal(pixels.reduce((a,b)=>a+b,0),255);
  const c=device.decodeDisplay({width:128,height:32,rotation:0,storage_bytes:1024,transfer_bytes:1024,format:'page-lsb-hex',data:'01'+'00'.repeat(511)+'ff'.repeat(512)});
  assert.equal(c.length,4096);assert.equal(c[0],255);assert.equal(c.reduce((a,b)=>a+b,0),255);
});

const page=(offset,revision=7)=>({width:128,height:64,rotation:0,format:'row-gray4-msb-hex',offset,total_bytes:4096,revision,next_offset:offset<3072?offset+1024:null,data:(offset/1024+1).toString(16).padStart(2,'0').repeat(1024)});
test('display pages pin one revision and return a complete frame',async()=>{
  const requests=[];
  const frame=await device.readDisplay(async path=>{requests.push(path);const offset=path==='/display'?0:Number(path.split('/')[2].split('?')[0]);return page(offset);});
  assert.deepEqual(requests,['/display','/display/1024?revision=7','/display/2048?revision=7','/display/3072?revision=7']);
  assert.equal(frame.data,'01'.repeat(1024)+'02'.repeat(1024)+'03'.repeat(1024)+'04'.repeat(1024));
});

test('display revision conflicts retry the whole frame at most three times',async()=>{
  let starts=0;
  const frame=await device.readDisplay(async path=>{if(path==='/display'){starts++;return page(0,starts);}if(starts===1)throw Object.assign(new Error('changed'),{status:409,code:'display_changed'});return page(Number(path.split('/')[2].split('?')[0]),starts);});
  assert.equal(starts,2);assert.equal(frame.revision,2);
  starts=0;
  await assert.rejects(device.readDisplay(async path=>{if(path==='/display'){starts++;return page(0);}throw Object.assign(new Error('changed'),{status:409,code:'display_changed'});}));
  assert.equal(starts,3);
  starts=0;
  await assert.rejects(device.readDisplay(async()=>{starts++;throw Object.assign(new Error('unrelated conflict'),{status:409,code:'other'});}));
  assert.equal(starts,1);
});

test('malformed display continuation never yields a partially valid image',async()=>{
  for(const change of [{offset:2048},{revision:9},{next_offset:3072},{data:''},{data:'z0'},{height:32},{total_bytes:4095}]) {
    let calls=0;await assert.rejects(device.readDisplay(async()=>{calls++;return calls===1?page(0):{...page(1024),...change};}));assert.equal(calls,2);
  }
  await assert.rejects(device.readDisplay(async()=>({...page(0),next_offset:4096,data:'00'.repeat(4096)})));
});
