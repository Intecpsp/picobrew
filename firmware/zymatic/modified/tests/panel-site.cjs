// Exercise the page as served, including gzip content, first contact and dial input.
const {readFileSync}=require("node:fs");
const {join}=require("node:path");
const {gunzipSync}=require("node:zlib");
const assert=require("node:assert/strict");
const vm=require("node:vm");
const site=join(__dirname,"../site");
const bytes=readFileSync(join(site,"zymatic_site.h"),"utf8").match(/0x[0-9a-f]{2}/g).map(Number);
const html=gunzipSync(Buffer.from(bytes)).toString();
assert.match(html,/Rinse and clean the inline filter after every brew/);
assert.match(html,/Deep clean every 3–4 brews, then rinse/);
assert.match(html,/<script src="\/pair.js"><\/script>/);
assert.match(html, /<link rel="icon" href="\/favicon\.ico">/);
const iconBytes=readFileSync(join(site,"zymatic_favicon.h"),"utf8").match(/0x[0-9a-f]{2}/g).map(Number);
const icon=gunzipSync(Buffer.from(iconBytes));
assert.deepEqual(icon,readFileSync(join(site,"favicon.ico")));
assert.equal(require("node:crypto").createHash("sha256").update(icon).digest("hex"),
  "b6eb1fd09848e2b533d0bbd5d497dc364536e484bd588e183a9b2a21baed58ee");
const jsBytes=readFileSync(join(site,"zymatic_pair_js.h"),"utf8").match(/0x[0-9a-f]{2}/g).map(Number);
const source=gunzipSync(Buffer.from(jsBytes)).toString();
const moves=[],pixels=[],requests=[];
const show=()=>env.form.onsubmit({preventDefault(){}});
let revision=321,mode="menu",conflict=false,copied,stored="",refreshOverview;
const env={
  location:{hash:"",pathname:"/",search:""},pair:{},
  document:{hidden:false},form:{},key:{value:""},owner:{hidden:true},message:{},info:{},journal:{},samples:{},
  Option:class {constructor(text,value){this.text=text;this.value=value}},
  list:{value:"",hidden:true,options:[],replaceChildren(...options){this.options=options;this.value=""},add(option){this.options.push(option)}},
  oled:{getContext(){return {fillRect(x,y){pixels[y*128+x]=this.fillStyle}}}},up:{},press:{},down:{},copy:{},forget:{},
  navigator:{clipboard:{async writeText(value){copied=value}}},
  sessionStorage:{getItem(){return stored},setItem(name,value){stored=value},removeItem(){stored=""}},
  setInterval(fn){refreshOverview=fn;},
  async fetch(url,options){
    const path=url.slice("/api/v1/".length);
    requests.push(path);
    if(path==="overview"){
      assert.equal(options.headers.Authorization,undefined);
      return {ok:true,json:async()=>({busy:mode==="press",wort_f:151.5,maintenance:{due:mode==="press"?["rinse","clean_filter","deep_clean"]:[]}})};
    }
    if(options.headers.Authorization!=="Bearer abc")return {ok:false,status:401};
    if(path==="history")return {ok:true,json:async()=>({sessions:[{id:7,recipe_name:"Pale",state:"interrupted",truncated:true}]})};
    if(path==="history/samples/0?id=7")return {ok:true,json:async()=>({samples:[[5000,151,180,152,1,0]],next_offset:1})};
    if(path==="history/samples/1?id=7")return {ok:true,json:async()=>({samples:[[9000,null,80,0,2,null]]})};
    if(path==="commands"){
      const cmd=JSON.parse(options.body);
      moves.push(cmd);assert.equal(options.method,"POST");
      if(conflict||cmd.display_revision!==revision)return {ok:false,status:409};
      revision++;return {ok:true,json:async()=>({accepted:true})};
    }
    const offset=path==="display"?0:Number(path.match(/^display\/(\d+)\?revision=(\d+)$/)[1]);
    if(offset)assert.equal(path.split("revision=")[1],String(revision));
    return {ok:true,json:async()=>({revision,dial:mode,data:(offset===896?"80":"01").repeat(128)})};
  }
};
vm.runInNewContext(source,env);
(async()=>{
  await new Promise(resolve=>setImmediate(resolve));
  assert.deepEqual(requests,["overview"]);
  assert.equal(env.info.value,"Monitoring / 151.5 °F");
  assert.equal(env.owner.hidden,true);
  env.key.value="wrong";await show();
  assert.equal(env.owner.hidden,true);assert.match(env.message.value,/device key/);
  env.key.value="abc";await show();
  assert.equal(env.owner.hidden,false);
  assert.equal(stored,"abc");
  await env.copy.onclick();assert.equal(copied,"abc");
  assert.equal(pixels[0],"white");assert.equal(pixels[128],"black");assert.equal(pixels[63*128],"white");
  await env.journal.onclick();assert.equal(env.list.options[1].text,"Pale · interrupted (partial)");
  env.list.value="7";await env.list.onchange();
  assert.match(env.samples.textContent,/5\t151\t180\t152\tMash\t1/);
  assert.match(env.samples.textContent,/9\t—\t80\tOff\tHop 1\t—/);
  const move=env.up.onclick();await env.down.onclick();await move;
  assert.deepEqual(moves,[{command:"encoder",display_revision:321,delta:-1}]);
  mode="press";refreshOverview();await new Promise(resolve=>setImmediate(resolve));
  assert.match(env.info.value,/Rinse.*check filter.*Deep clean due/);await show();
  assert.equal(env.up.disabled,true);assert.equal(env.down.disabled,true);assert.equal(env.press.disabled,false);
  mode="off";await show();assert.equal(env.press.disabled,true);
  mode="menu";await show();conflict=true;
  await env.press.onclick();
  assert.deepEqual(moves[1],{command:"encoder",display_revision:322,click:true});
  assert.equal(env.owner.hidden,true);assert.match(env.message.value,/Refresh/);
  env.forget.onclick();assert.equal(env.key.value,"");assert.equal(stored,"");assert.equal(env.list.hidden,true);assert.equal(env.samples.textContent,"");
  for(const hub of [false,true]){
    const nonce="ab".repeat(32), credential="cd".repeat(32);
    const paths=[];
    const machine={...env,key:{value:""},owner:{hidden:true},info:{},pair:{},
      location:{hash:hub?"#pair="+nonce:"",pathname:"/",search:""},
      crypto:{getRandomValues(bytes){bytes.fill(0xab);return bytes}},
      history:{replaceState(_state,_title,url){assert.equal(url,"/");machine.location.hash=""}},
      async fetch(url,options){
        assert.equal(machine.location.hash,"");
        const path=url.slice("/api/v1/".length);paths.push(path);
        if(path.startsWith("pairing/")){
          assert.equal(options.headers.Authorization,undefined);
          assert.deepEqual(JSON.parse(options.body),{nonce});
          return {ok:true,json:async()=>path.endsWith("request")?{state:"pending",code:"abababab"}:{api_key:credential}};
        }
        if(path.startsWith("display"))assert.equal(options.headers.Authorization,"Bearer "+credential);
        return {ok:true,json:async()=>({revision:1,dial:"off",data:"00".repeat(128)})};
      }
    };
    const context=vm.createContext(machine);
    vm.runInContext(source,context);
    if(!hub)await machine.pair.onclick();
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(machine.owner.hidden,false);
    assert.equal(stored,credential);
    assert.equal(paths.includes("pairing/request"),!hub);
    assert.ok(paths.includes("pairing/claim"));
    machine.forget.onclick();
  }
})().catch(error=>{console.error(error);process.exitCode=1});
