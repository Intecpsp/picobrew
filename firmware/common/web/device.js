'use strict';

// Shared presentation of the standalone API. Capability flags, rather than a
// guessed hardware family, decide which controls and reads the browser uses.
const PicoDevice = (() => {
  const names = {'pico-s':'Pico S','pico-pro':'Pico Pro','pico-c':'Pico C',
    'z-series':'Pico Z series',picoferm:'PicoFerm',picostill:'PicoStill'};
  const legacy = info => (!info.model && Number(info.recipe_capacity)>0) || ['pico-s','pico-pro','pico-c'].includes(info.model);
  const feature = (info,name) => typeof info.capabilities?.[name] === 'boolean' ? info.capabilities[name] :
    typeof info[name] === 'boolean' ? info[name] :
    name === 'recipes' ? Number(info.recipe_capacity)>0 :
    ['brewing','heating','temperature','pump'].includes(name) ? legacy(info) : false;
  const label = info => info.model_name || names[info.model] || 'Pico device';
  const finite = value => typeof value === 'number' && Number.isFinite(value);
  const commands = info => Array.isArray(info.commands) ? info.commands.filter(value=>typeof value==='string') : [];
  const commandLabels = {start:'Start',stop:'Stop',clean:'Clean',drain:'Drain',chill:'Chill',pause:'Pause session',resume:'Resume session',
    'essential-oils':'Essential oils','start-fermentation':'Start fermentation',
    'stop-fermentation':'Stop fermentation',calibrate:'Calibrate pressure',sample:'Take a reading',
    'set-pump':'Set pump','set-fan':'Set fan','set-sampling-interval':'Set sampling interval'};
  function decodeDisplay(frame) {
    const width=frame.width,height=frame.height;
    if(width!==128 || ![32,64].includes(height)) throw new Error('Unsupported display dimensions.');
    const gray=frame.format==='row-gray4-msb-hex';
    if(gray && (height!==64 || frame.rotation==null))throw new Error('Unsupported grayscale display geometry.');
    if(!gray && frame.format!=='page-lsb-hex') throw new Error('Unsupported display format.');
    const size=gray?width*height/2:width*Math.ceil(height/8), storage=frame.storage_bytes ?? size;
    const rotation=frame.rotation ?? (gray?0:2), transfer=frame.transfer_bytes ?? storage;
    if(![0,2].includes(rotation) || !Number.isInteger(storage) || storage<size || storage>(gray?4096:1024) ||
       !Number.isInteger(transfer) || transfer<size || transfer>storage ||
       typeof frame.data!=='string' || frame.data.length!==storage*2 || !/^[0-9a-f]+$/i.test(frame.data))
      throw new Error('Incomplete display frame.');
    const bytes=Uint8Array.from({length:storage},(_,i)=>parseInt(frame.data.slice(i*2,i*2+2),16));
    const pixels=new Uint8Array(width*height);
    for(let y=0;y<height;y++) for(let x=0;x<width;x++) {
      const brightness=gray?((bytes[y*(width/2)+(x>>1)]>>(x&1?0:4))&15)*17:
        ((bytes[(y>>3)*width+x]>>(y&7))&1)*255;
      pixels[rotation===2?(height-1-y)*width+width-1-x:y*width+x]=brightness;
    }
    return pixels;
  }
  async function readDisplay(api) {
    for(let attempt=0;attempt<3;attempt++) {
      try {
        const first=await api('/display');
        if(first.next_offset==null) {decodeDisplay(first);return first;}
        const total=first.total_bytes,revision=first.revision;
        if(first.offset!==0 || !Number.isSafeInteger(total) || total<1 || total>4096 ||
           !Number.isSafeInteger(revision) || revision<0 || typeof first.data!=='string' ||
           !/^(?:[0-9a-f]{2})+$/i.test(first.data) || first.data.length>2048 ||
           first.next_offset!==first.data.length/2 || first.next_offset>=total) throw new Error('Invalid display page.');
        let data=first.data, offset=first.next_offset;
        while(offset<total) {
          const page=await api('/display/'+offset+'?revision='+revision);
          if(page.offset!==offset || page.total_bytes!==total || page.revision!==revision ||
             ['width','height','format','rotation','storage_bytes','transfer_bytes'].some(key=>page[key]!==first[key]) ||
             typeof page.data!=='string' || !/^(?:[0-9a-f]{2})+$/i.test(page.data) || page.data.length>2048 ||
             offset+page.data.length/2>total) throw new Error('Inconsistent display page.');
          data+=page.data;offset+=page.data.length/2;
          if((offset<total && page.next_offset!==offset) || (offset===total && page.next_offset!=null))
            throw new Error('Invalid display continuation.');
        }
        const frame={...first,data,next_offset:null};decodeDisplay(frame);return frame;
      } catch(error) {if(error.status!==409 || error.code!=='display_changed' || attempt===2)throw error;}
    }
  }
  return {feature,label,finite,commands,commandLabels,decodeDisplay,readDisplay};
})();
if(typeof module==='object' && module.exports)module.exports=PicoDevice;
