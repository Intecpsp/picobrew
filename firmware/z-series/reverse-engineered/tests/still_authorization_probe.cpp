#define ARDUINOJSON_USE_DOUBLE 1
#define ARDUINOJSON_USE_LONG_LONG 0
#define ARDUINOJSON_ENABLE_COMMENTS 1
#include <ArduinoJson.h>
#include "zseries/still_authorization.hpp"
#include "zseries/pico_still.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
struct Context {ArduinoJson::JsonVariantConst input;ArduinoJson::JsonArray effects,assertions,delays;std::uint32_t clock=1000,counter=0;};
Context* active;
void event(const char* name,std::uint32_t value){auto row=active->effects.createNestedArray();row.add(name);row.add(value);}
bool report(void*,ConsoleLevel,const char*,const char*,std::uint32_t line,const char*){event("diagnostic",line);return true;}
bool assertion(void*,const char* file,std::uint32_t line,const char* expression){auto row=active->assertions.createNestedArray();row.add(file);row.add(line);row.add(expression);event("diagnostic",line);active->delays.add(200);++active->counter;active->clock+=200;return true;}
bool select(void*){return !(active->input["select_fail"]|false);}
const char* text(ArduinoJson::JsonVariantConst root,const char* key,const char* fallback){if(root.containsKey(key)&&root[key].isNull())return nullptr;const auto v=root[key].as<const char*>();return v?v:fallback;}
void fill(char* out,std::size_t capacity,const char* value){if(value)std::strncpy(out,value,capacity-1);}
}
namespace zseries { namespace oem {
HttpResult HttpClient::configure(const char* uri,const HttpReceiver& sink,HttpClientClass supplied,bool& accepted){
    auto row=active->effects.createNestedArray();row.add("configure");row.add(std::string(uri));row.add(static_cast<std::uint32_t>(supplied));receiver=sink;accepted=active->input["configured"]|true;return (active->input["configure_missing"]|false)?HttpResult::MissingService:HttpResult::Ok;}
bool HttpClient::set_authorization(const char*) noexcept{event("authorization",0);return true;}
HttpResult HttpClient::post(const char* type,const void* bytes,std::size_t count,bool& accepted){auto row=active->effects.createNestedArray();row.add("POST");row.add(type);row.add(std::string(static_cast<const char*>(bytes),count));accepted=active->input["requested"]|true;return HttpResult::Ok;}
}}
int main(){std::string line;while(std::getline(std::cin,line)){
    ArduinoJson::DynamicJsonDocument input(32768),output(32768);if(ArduinoJson::deserializeJson(input,line))return 2;const auto in=input.as<ArduinoJson::JsonVariantConst>();
    output["id"]=in["id"].as<const char*>();Context context;context.input=in;context.effects=output.createNestedArray("effects");context.assertions=output.createNestedArray("assertions");context.delays=output.createNestedArray("delays");active=&context;
    HttpClient http;http.state=static_cast<HttpState>(in["http_state"]|0u);StillAuthorization auth(http);auth.authorization="fixture-authorization";auth.services.select_wifi_tls=select;auth.services.diagnostics.assertion=assertion;auth.services.diagnostics.log=report;
    auth.pending=in["pending"]|false;auth.frozen=in["frozen"]|false;auth.truncated=in["truncated"]|false;
    fill(auth.buffer.data(),auth.buffer.size(),text(in,"initial_body",""));auth.length=std::strlen(auth.buffer.data());auth.remaining=512-auth.length;
    fill(auth.record.uid.data(),auth.record.uid.size(),text(in,"initial_uid","previous"));fill(auth.record.cleaned_ack_date.data(),auth.record.cleaned_ack_date.size(),text(in,"initial_date","previous-date"));auth.record.profile_id=in["initial_profile"]|99;
    if(in["missing_assertion"]|false)auth.services.diagnostics.assertion=nullptr;
    if(in["missing_log"]|false)auth.services.diagnostics.log=nullptr;
    if(in["missing_select"]|false)auth.services.select_wifi_tls=nullptr;
    if(in["missing_authorization"]|false)auth.authorization=nullptr;
    const std::string op=text(in,"op","");StillAuthorizationResult result=StillAuthorizationResult::Ok;bool accepted=false;
    if(op=="request"||op=="roundtrip")result=auth.request(text(in,"machine","fixture-machine"),text(in,"still","fixture-still"),in["cleaned"]|false);
    if(op=="prepare")result=auth.prepare_body(text(in,"machine","fixture-machine"),text(in,"still","fixture-still"),in["cleaned"]|false);
    if(op=="headers"||op=="roundtrip"){HttpResponse response;response.content_length=in["content_length"]|-1;response.status=in["status"]|200;fill(response.content_type.data(),response.content_type.size(),text(in,"content_type","application/json"));result=auth.receive_headers(response,accepted);if(op=="headers")output["accepted"]=accepted;}
    if(op=="body"||op=="roundtrip"){
        if(in["chunks"].is<ArduinoJson::JsonArrayConst>()){for(auto chunk:in["chunks"].as<ArduinoJson::JsonArrayConst>()){std::string value=chunk.as<const char*>();result=auth.receive_body(reinterpret_cast<const std::uint8_t*>(value.data()),value.size());}}
        else {const char* value=text(in,"body","");const auto count=in["count"]|(value?std::strlen(value):0u);result=auth.receive_body(reinterpret_cast<const std::uint8_t*>(value),count);}
    }
    if(op=="parse"||op=="tick"){
        auth.buffer.fill(0);fill(auth.buffer.data(),auth.buffer.size(),text(in,"json","{}"));auth.length=std::strlen(auth.buffer.data());auth.remaining=512-auth.length;
        if(op=="parse"){result=auth.parse();output["accepted"]=auth.json_error==RecipeJsonError::Ok;}else result=auth.tick();
    }
    if(op=="roundtrip"){auth.receiver().complete(&auth);http.state=HttpState::Idle;result=auth.tick();}
    if(op=="complete")auth.receiver().complete(&auth);
    output["result"]=static_cast<unsigned>(result);output["pending"]=auth.pending;auto record=output.createNestedObject("record");record["uid"]=std::string(auth.record.uid.data());record["profile"]=static_cast<std::uint32_t>(auth.record.profile_id);record["date"]=std::string(auth.record.cleaned_ack_date.data());record["uid_truncated"]=auth.record.uid_truncated;record["date_truncated"]=auth.record.cleaned_truncated;
    if(op!="parse"&&op!="tick"&&op!="roundtrip")output["buffer"]=std::string(auth.buffer.data());
    output["length"]=auth.length;output["remaining"]=auth.remaining;output["truncated"]=auth.truncated;output["frozen"]=auth.frozen;
    PicoStillObserved observed;auth.observe(observed);auto state=output.createNestedArray("observed");state.add(observed.authorization_pending);state.add(observed.authorized);state.add(observed.cleaned);output["counter"]=context.counter;output["clock"]=context.clock;
    ArduinoJson::serializeJson(output,std::cout);std::cout<<'\n';
}}
