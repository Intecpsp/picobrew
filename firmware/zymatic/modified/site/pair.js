// A fresh ticket identifies only this browser. The physical dial approves it.
async function pairDevice(nonce) {
  pair.disabled=true;
  try {
    if(!nonce){
      const bytes=crypto.getRandomValues(new Uint8Array(32));
      nonce=Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
      const pending=await api('pairing/request',{nonce},true);
      if(pending.code!==nonce.slice(0,8))throw Error();
      message.value='Open About Zymatic and approve code '+pending.code+'.';
    }
    for(let i=0;i<90;i++){
      const reply=await api('pairing/claim',{nonce},true);
      if(/^[0-9a-f]{64}$/.test(reply.api_key||'')){
        key.value=reply.api_key;
        await show();
        return;
      }
      if(reply.state!=='pending')break;
      await new Promise(resolve=>setTimeout(resolve,1000));
    }
    throw Error();
  }catch{message.value='Pairing unavailable. Start again.'}
  finally{pair.disabled=false}
}
pair.onclick=()=>pairDevice('');
if(hubTicket)pairDevice(hubTicket);
