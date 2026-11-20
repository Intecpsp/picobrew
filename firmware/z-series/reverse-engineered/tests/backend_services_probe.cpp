// Native backend probe. HTTP/MQTT calls are explicit comparison boundaries;
// production modules link to src/http_client.cpp and the platform services.
#define ARDUINOJSON_USE_DOUBLE 1
#define ARDUINOJSON_USE_LONG_LONG 0
#define ARDUINOJSON_ENABLE_COMMENTS 1
#include <ArduinoJson.h>
#include "zseries/z_backend.hpp"
#include "zseries/http_backend.hpp"
#include "zseries/azure.hpp"
#include "zseries/backend_console.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
using namespace zseries::oem;
namespace {
struct Context {ArduinoJson::JsonVariantConst input;ArduinoJson::JsonArray effects,assertions,delays;
    std::uint32_t clock=1000,counter=0;bool published=false;};
Context* active;
void event(const char* type,const char* text=nullptr,std::uint32_t value=0){auto row=active->effects.createNestedArray();row.add(type);if(text)row.add(std::string(text));else row.add(value);}
bool assertion(void* p,std::uint32_t line){auto& c=*static_cast<Context*>(p);c.assertions.add(line);++c.counter;c.delays.add(200);c.clock+=200;return true;}
bool diagnostic(void*,std::uint32_t line){event("diagnostic",nullptr,line);return true;}
bool network(void*,bool& result){event("network");result=active->input["network"]|false;return true;}
std::uint32_t now(void*){return active->clock;}
bool azure_perform(void*,const AzureAction& action,std::uint32_t& result){if(std::strcmp(active->input["op"].as<const char*>(),"azure_sas")==0){event("diagnostic",nullptr,action.argument);result=0;return true;}auto row=active->effects.createNestedArray();row.add(static_cast<std::uint32_t>(action.effect));row.add(action.argument);
    const auto words=active->input["words"];result=1;if(action.effect==AzureEffect::Publish)active->published=true;
    if(action.effect==AzureEffect::Connected)result=words[14].as<std::uint32_t>();
    else if(action.effect==AzureEffect::AcquireClient)result=words[15].as<std::uint32_t>();
    else if(action.effect==AzureEffect::Connect)result=words[16].as<std::uint32_t>();
    return true;
}
bool console_assertion(void* c,const char*,std::uint32_t line,const char*){return assertion(c,line);}
bool console_log(void*,ConsoleLevel,const char*,const char*,std::uint32_t,const char*){return true;}
bool fixture_callback(void*){return true;}
int fixture_hmac(void*,const std::uint8_t*,std::size_t,const std::uint8_t* data,std::size_t size,std::uint8_t out[32]){event("hmac_input",std::string(reinterpret_cast<const char*>(data),size).c_str());for(unsigned i=0;i<32;++i)out[i]=static_cast<std::uint8_t>((i*17u+(active->input["seed"]|0u))&255u);return (active->input["hmac_fail"]|false)?-7:0;}
const char* text(ArduinoJson::JsonVariantConst root,const char* key,const char* fallback){const char* value=root[key].as<const char*>();return value?value:fallback;}
Quantity quantity(ArduinoJson::JsonVariantConst root,const char* key){auto value=root[key];return {value[0].as<double>(),value.isNull()?Unit::None:static_cast<Unit>(value[1].as<std::uint32_t>())};}
void parameters(ProgramParameters& p,ArduinoJson::JsonVariantConst in){p.id=static_cast<ZProgramId>(in["program"]|0u);p.abv=in["abv"]|-1;p.ibu=in["ibu"]|-1;
    p.time=quantity(in,"duration");p.water=quantity(in,"water");p.intensity=quantity(in,"intensity");p.temperature=quantity(in,"temperature");}
SessionSnapshot session(ArduinoJson::JsonVariantConst in){SessionSnapshot s;s.type=static_cast<SessionType>(in["type"]|0u);s.program=static_cast<ZProgramId>(in["program"]|0u);
    s.recipe_id=in["recipe"]|0xffffffffu;s.session_id=in["session"]|0u;s.name=text(in,"name","Session");s.stats.duration_seconds=in["seconds"]|0u;
    s.stats.pressure_pascal=in["pressure"]|0.;s.stats.maximum_temperature_celsius=in["maximum"]|0.;s.stats.maximum_temperature_added_seconds=in["added"]|0u;
    s.group_session=in["group"].as<bool>();parameters(s.parameters,in);s.recipe_guid=text(in,"guid","guid");s.still_uid=text(in,"uid","still");s.still_version=text(in,"version","1.0");return s;}
}
namespace zseries { namespace oem {
HttpResult HttpClient::configure(const char* uri,const HttpReceiver& value,HttpClientClass,bool& accepted){event("configure",uri);receiver=value;accepted=active->input["configured"]|true;return HttpResult::Ok;}
bool HttpClient::set_authorization(const char*) noexcept{event("authorization");return true;}
HttpResult HttpClient::get(bool& accepted){event("GET");accepted=active->input["requested"]|true;return HttpResult::Ok;}
HttpResult HttpClient::post(const char*,const void* bytes,std::size_t size,bool& accepted){event("POST",std::string(static_cast<const char*>(bytes),size).c_str());accepted=active->input["requested"]|true;return HttpResult::Ok;}
HttpResult HttpClient::put(const char*,const void* bytes,std::size_t size,bool& accepted){event("PUT",std::string(static_cast<const char*>(bytes),size).c_str());accepted=active->input["requested"]|true;return HttpResult::Ok;}
HttpResult HttpClient::tick(std::uint32_t stamp){event("tick",nullptr,stamp);return HttpResult::Ok;}
HttpResult HttpClient::cancel(){event("cancel");return HttpResult::Ok;}
}}
int main(){(void)now;std::string line;while(std::getline(std::cin,line)){
    ArduinoJson::DynamicJsonDocument input(32768),output(65536);if(ArduinoJson::deserializeJson(input,line)!=ArduinoJson::DeserializationError::Ok)return 2;
    auto root=input.as<ArduinoJson::JsonVariantConst>();output["id"]=root["id"].as<const char*>();Context context;context.input=root;
    context.effects=output.createNestedArray("effects");context.assertions=output.createNestedArray("assertions");context.delays=output.createNestedArray("delays");active=&context;
    BackendHooks hooks;hooks.context=&context;hooks.assertion=assertion;hooks.diagnostic=diagnostic;hooks.network_ready=network;
    const std::string op=text(root,"op","");
    if(op=="service_checks"){
        HttpClient client;ZBackend z(client);bool ready=true;output["z_missing_network"]=static_cast<unsigned>(z.can_sync(ready));output["z_ready"]=ready;
        BackendState registered;ProgramBuffer program;CoreEvent event_value;CoreEventMailbox events;HttpBackend http(client,registered,program,event_value,events);output["http_missing_network"]=static_cast<unsigned>(http.registration());output["http_state"]=http.state;
        PendingEvent<AzureCommand> commands;AzureService azure(commands);output["azure_missing_transport"]=static_cast<unsigned>(azure.tick(1));azure.device_id="fixture-device";const std::uint8_t key[]={1};azure.key=key;azure.key_size=1;output["azure_missing_hmac"]=static_cast<unsigned>(azure.create_password("123"));output["password"]=std::string(azure.password.text.data());
        BackendConsoleCallbacks callbacks;ConsoleEntry entries[2];ConsoleServices console_services;console_services.context=&context;console_services.assertion=console_assertion;console_services.log=console_log;ConsoleShell shell("fixture",entries,2,console_services);output["console_missing_callbacks"]=static_cast<unsigned>(register_z_backend_console(shell,callbacks));output["console_entries"]=shell.size();
    }else if(op=="session"){
        std::vector<char> buffer(root["capacity"]|1024u);serialize_session_json(buffer.data(),buffer.size(),session(root),hooks);output["json"]=std::string(buffer.data());
        output.remove("effects");
    }else if(op=="created"){
        HttpClient client;ZBackend backend(client);backend.hooks=hooks;backend.state=root["state"]|1u;backend.created_session_id=root["created"]|91u;
        std::strncpy(backend.response.data(),text(root,"json",""),backend.response.size()-1);backend.response_length=std::strlen(backend.response.data());
        backend.receive_complete();output["state"]=backend.state;output["created"]=backend.created_session_id;output["completed"]=backend.completed?1:0;output["frozen"]=backend.response_frozen?1:0;output.remove("effects");
    }else if(op=="http_parse"||op=="http_start"||op=="http_tick"){
        HttpClient client;BackendState state;ProgramBuffer program;CoreEvent ev;CoreEventMailbox queue;HttpBackend backend(client,state,program,ev,queue);
        backend.hooks=hooks;backend.token=text(root,"token","fixture-token");backend.authorization="fixture-authorization";
        backend.state=root["state"]|2u;backend.event.source=static_cast<CoreCommandId>(root["source"]|3u);
        backend.offset=root["offset"]|0;backend.reference_id=root["reference"]|0;backend.pending_recipe.id=(op=="http_tick"&&backend.state==9)?0u:root["recipe"]|42u;
        backend.search=text(root,"search","");backend.local_recipe.uri.set(text(root,"uri","http://local/recipe"));
        backend.backend.backend_error=static_cast<ZBackendError>(root["backend_error"]|0u);
        bool accepted=false;
        if(op=="http_parse"){
            std::strncpy(backend.response.data(),text(root,"json",""),backend.response.size()-1);backend.response_length=std::strlen(backend.response.data());backend.parse_response(accepted);
            output["accepted"]=accepted;output["frozen"]=backend.response_frozen?1:0;output["event"]=static_cast<std::uint32_t>(ev.type);output["source"]=static_cast<std::uint32_t>(ev.source);
            output["count"]=ev.count;output["offset"]=ev.offset;output["total"]=ev.total;output["overflow"]=ev.byte_ddc?1:0;
            auto recipes=output.createNestedArray("recipes");for(std::size_t i=0;i<ev.count;++i){auto row=recipes.createNestedObject();const auto& r=ev.recipes[i];row["id"]=r.id;row["kind"]=static_cast<std::uint32_t>(r.kind);row["name"]=std::string(r.name.data());row["length"]=r.name_length;row["truncated"]=r.name_overflow?1:0;row["abv"]=static_cast<std::uint32_t>(r.parameters.abv);row["ibu"]=static_cast<std::uint32_t>(r.parameters.ibu);}
            output["local_uri"]=std::string(backend.local_recipe.uri.text.data());output["local_guid"]=std::string(backend.local_recipe.guid.text.data());output.remove("effects");
        }else if(op=="http_tick"){client.state=static_cast<HttpState>(root["client_state"]|0u);client.error=static_cast<HttpClientErrorId>(root["client_error"]|0u);client.response.status=root["status"]|503u;std::strncpy(backend.response.data(),text(root,"json","{}"),backend.response.size()-1);backend.response_length=std::strlen(backend.response.data());backend.tick(root["now"]|100u);output["state"]=backend.state;output["uri"]=std::string(backend.uri.text.data());output["queued"]=queue.pending?static_cast<std::uint32_t>(queue.value.type):0;output["queued_source"]=queue.pending?static_cast<std::uint32_t>(queue.value.source):0;}
        else{const auto requested=backend.state;backend.state=requested-1;backend.start_request(requested);output["state"]=backend.state;output["uri"]=std::string(backend.uri.text.data());}
        auto back=output.createNestedObject("backend");back["registered"]=state.registered?1:0;back["ready"]=state.ready?1:0;back["failed"]=state.error?1:0;back["expired"]=state.token_expired?1:0;back["error"]=static_cast<std::uint32_t>(state.backend_error);
        back["text"]=std::string(state.error_text.text.data());back["token"]=std::string(state.registration_token.text.data());back["update"]=std::string(state.update_address.text.data());back["program_uri"]=std::string(state.program_uri.text.data());back["alias"]=std::string(state.alias.text.data());
        back["dirty"]=static_cast<std::uint32_t>(state.dirty_sessions);back["last"]=static_cast<std::uint32_t>(state.last_session);back["resume"]=static_cast<std::uint32_t>(state.resumable_session_id);
    }else if(op=="z_endpoint"||op=="z_sync"||op=="z_tick"||op=="z_create"||op=="z_end"||op=="z_log"){
        HttpClient client;ZBackend backend(client);backend.hooks=hooks;backend.token=text(root,"token","fixture-token");backend.authorization="fixture-authorization";backend.state=root["state"]|0u;
        client.state=static_cast<HttpState>(root["client_state"]|0u);client.error=static_cast<HttpClientErrorId>(root["client_error"]|0u);
        if(op=="z_endpoint")backend.endpoint(text(root,"type","ZSession"),backend.token,root["session"]|-1);
        else if(op=="z_sync"){bool ready=false;backend.can_sync(ready);output["ready"]=ready;}
        else if(op=="z_tick")backend.tick(root["now"]|100u);
        else if(op=="z_create")backend.create(session(root));else if(op=="z_end")backend.end(session(root));
        else{SessionLogSnapshot v;v.session_id=root["session"]|42u;v.thermoblock=root["thermoblock"]|98.;v.wort=root["wort"]|65.;v.ambient=root["ambient"]|22.;v.drain=root["drain"]|30.;v.target=root["target"]|67.;v.step_name=text(root,"step","Mash");v.valve_position=root["valve"]|3u;v.keg_pump=(root["pumps"]|0x30u)&0x10;v.drain_pump=(root["pumps"]|0x30u)&0x20;v.pause_reason=root["pause"]|0u;v.rssi=root["rssi"]|-65;v.seconds_remaining=root["remaining"]|123u;client.timing.send_duration=root["send"]|31u;client.timing.reply_duration=root["wait"]|42u;client.timing.body_duration=root["recv"]|53u;backend.log(v);}
        output["state"]=backend.state;output["uri"]=std::string(backend.uri.data());output["truncated"]=backend.uri_truncated?1:0;
    }else if(op=="backend_console"){
        unsigned kind=root["kind"]|0u;const char* names[]={HttpServerConsoleName,HttpBackendConsoleName,ZBackendConsoleName,AzureConsoleName};ConsoleEntry entries[8];ConsoleServices services;services.context=&context;services.assertion=console_assertion;services.log=console_log;ConsoleShell shell(names[kind],entries,8,services);BackendConsoleCallbacks callbacks;callbacks.information=fixture_callback;callbacks.tests=fixture_callback;LocalHttpServer server;HttpServerConsoleCallbacks server_callbacks;server_callbacks.server=&server;server_callbacks.information=fixture_callback;PendingEvent<AzureCommand> queue;AzureService azure(queue);AzureConsoleCallbacks azure_callbacks;azure_callbacks.azure=&azure;azure_callbacks.information=fixture_callback;azure_callbacks.tests=fixture_callback;
        if(kind==0)register_http_server_console(shell,server_callbacks);else if(kind==1)register_http_backend_console(shell,callbacks);else if(kind==2)register_z_backend_console(shell,callbacks);else register_azure_console(shell,azure_callbacks);output["name"]=shell.name();auto out=output.createNestedArray("entries");for(std::size_t i=0;i<shell.size();++i){auto row=out.createNestedArray();const auto* v=shell.entry(i);row.add(v->exact_match?1:0);row.add(v->token);row.add(v->description);}
    }else if(op=="azure_sas"){
        PendingEvent<AzureCommand> queue;AzureService azure(queue);azure.services.context=&context;azure.services.assertion=assertion;azure.services.perform=azure_perform;azure.services.hmac=fixture_hmac;azure.device_id=text(root,"device","fixture-device");azure.host=text(root,"host","picobrew.azure-devices.net");const std::uint8_t key[]={1,2,3,4};azure.key=key;azure.key_size=sizeof(key);azure.create_password(text(root,"expiry","2524636800"));output["password"]=std::string(azure.password.text.data());output["truncated"]=azure.password.truncated?1:0;
    }else if(op=="azure_receive"){
        PendingEvent<AzureCommand> queue;AzureService azure(queue);azure.services.context=&context;azure.services.millis=now;azure.services.perform=azure_perform;azure.services.assertion=assertion;
        azure.state=static_cast<AzureState>(root["state"]|2u);azure.core_state=static_cast<CoreStateId>(root["core"]|11u);azure.group_disabled=(root["disabled"]|0u)==1;
        azure.changed_ms=root["changed"]|0u;azure.deadline=root["deadline"]|0u;std::string payload=text(root,"json","");std::vector<char> buffer(payload.begin(),payload.end());buffer.push_back(0);azure.receive(nullptr,buffer.data(),payload.size());
        output["state"]=static_cast<std::uint32_t>(azure.state);output["changed"]=azure.changed_ms;output["deadline"]=azure.deadline;output["command"]=queue.pending?static_cast<std::uint32_t>(queue.value.id):0;
        output.remove("effects");auto effects=output.createNestedArray("effects");if(context.published){auto row=effects.createNestedArray();row.add("publish");row.add("state");}
        auto data=output.createNestedObject("data");if(queue.pending){if(queue.value.id==CoreCommandId::AzureSessionLoad){const auto& v=queue.value.session;data["name"]=std::string(v.name.data());data["type"]=static_cast<std::uint32_t>(v.type);data["recipe"]=v.recipe_id;data["program"]=static_cast<std::uint32_t>(v.program);data["group"]=v.group_session?1:0;auto params=data.createNestedObject("parameters");params["id"]=static_cast<std::uint32_t>(v.parameters.id);params["abv"]=static_cast<std::uint32_t>(v.parameters.abv);params["ibu"]=static_cast<std::uint32_t>(v.parameters.ibu);
            const Quantity values[]={v.parameters.time,v.parameters.water,v.parameters.intensity,v.parameters.temperature};const char* names[]={"duration","water","intensity","temperature"};for(unsigned i=0;i<4;++i){std::uint64_t bits;std::memcpy(&bits,&values[i].value,8);char text[17];std::snprintf(text,sizeof(text),"%016llx",static_cast<unsigned long long>(bits));auto q=params.createNestedArray(names[i]);q.add(std::string(text));q.add(static_cast<std::uint32_t>(values[i].unit));}}
            else{const auto& v=queue.value.recipe;data["id"]=v.id;data["name"]=std::string(v.name.data());data["kind"]=static_cast<std::uint32_t>(v.kind);data["abv"]=static_cast<std::uint32_t>(v.parameters.abv);data["ibu"]=static_cast<std::uint32_t>(v.parameters.ibu);}}
    }else if(op=="azure"){
        auto input_words=root["words"];std::uint32_t words[22];for(unsigned i=0;i<22;++i)words[i]=input_words[i].as<std::uint32_t>();
        context.clock=words[2];context.counter=words[3];PendingEvent<AzureCommand> queue;queue.pending=words[18];queue.value.id=static_cast<CoreCommandId>(words[19]);
        AzureService azure(queue);azure.services.context=&context;azure.services.millis=now;azure.services.perform=azure_perform;azure.services.assertion=assertion;
        azure.state=static_cast<AzureState>(words[4]);azure.enabled=words[5];azure.changed_ms=words[6];azure.deadline=words[7];azure.backoff.active=words[8];azure.backoff.attempts=words[9];azure.backoff.deadline=words[10];azure.backoff.duration=words[11];azure.backoff.initial_duration=words[12];azure.backoff.maximum_duration=words[13];
        switch(words[0]){case 0:azure.tick(words[1]);break;case 1:azure.transition(static_cast<AzureState>(words[17]));break;case 2:azure.connect();break;case 3:azure.session_begin();break;case 4:azure.session_end();break;case 5:azure.session_discard();break;case 6:azure.recover(words[21]==1);break;}
        words[2]=context.clock;words[3]=context.counter;words[4]=static_cast<std::uint32_t>(azure.state);words[5]=azure.enabled;words[6]=azure.changed_ms;words[7]=azure.deadline;words[8]=azure.backoff.active;words[9]=azure.backoff.attempts;words[10]=azure.backoff.deadline;words[11]=azure.backoff.duration;words[12]=azure.backoff.initial_duration;words[13]=azure.backoff.maximum_duration;words[18]=queue.pending;words[19]=static_cast<std::uint32_t>(queue.value.id);
        auto out=output.createNestedArray("words");for(auto w:words)out.add(w);
    }else return 3;
    output["counter"]=context.counter;ArduinoJson::serializeJson(output,std::cout);std::cout<<'\n';
}}
