#include "zseries/network_console_tests.hpp"
#include "zseries/backend_diagnostics.hpp"
#include "oem_credentials.hpp"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
using namespace zseries::oem;
namespace {
struct Context {
    std::uint32_t clock=0,counter=0,water=4096,factory=1,connect=1,yield_ms=200;
    std::size_t position=0,requests=0,current=0;
    std::vector<std::string> wires,events;
    HttpClient client;BackendState state;ProgramBuffer program;CoreEvent event;CoreEventMailbox mailbox;
    HttpBackend backend;std::array<char,13> identifier{};
    unsigned missing=0,failure=0,perform_count=0;
    Context():backend(client,state,program,event,mailbox){}
};
bool assertion(void* opaque,const char* file,std::uint32_t line,const char* expression){auto& c=*static_cast<Context*>(opaque);
    c.events.push_back("assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression);++c.counter;c.clock+=200;return true;}
bool log(void* opaque,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){auto& c=*static_cast<Context*>(opaque);
    c.events.push_back("log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(line)+"|"+message);
    return c.failure!=1&&!(c.failure==5&&!file&&!std::strcmp(tag,"HBE"))&&
        !(c.failure==6&&line==0x14f&&!std::strcmp(tag,"JSON"));}
bool scope(void* opaque,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){auto& c=*static_cast<Context*>(opaque);
    c.events.push_back("scope|"+std::to_string(begin?1:0)+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+name);
    if(!begin&&c.water<=700)assertion(opaque,"Assert.h",0x48,"stackHighWaterMark > 700");
    return c.failure!=2;}
bool perform(void* opaque,const HttpAction& action,std::uint32_t& result){auto& c=*static_cast<Context*>(opaque);
    if(c.failure==3&&++c.perform_count==3)return false;
    const auto number=static_cast<unsigned>(action.effect);result=0;
    switch(action.effect){
    case HttpEffect::AcquireClient:
        if(!action.acquired_class||c.requests>=c.wires.size())return false;
        c.current=c.requests++;c.position=0;result=c.factory;*action.acquired_class=c.factory?HttpClientClass::WifiSecure:HttpClientClass::None;
        c.events.push_back("io|0|"+std::to_string(action.argument));return true;
    case HttpEffect::FreeHeap:result=100000;break;
    case HttpEffect::ConnectHost:result=c.connect;c.position=0;c.events.push_back("host|"+std::to_string(action.argument)+"|"+action.text);return true;
    case HttpEffect::Write:result=static_cast<std::uint32_t>(action.size);c.events.push_back("write_bytes|"+std::string(reinterpret_cast<const char*>(action.data),action.size));return true;
    case HttpEffect::Flush:break;
    case HttpEffect::Available:result=static_cast<std::uint32_t>(c.wires[c.current].size()-c.position);break;
    case HttpEffect::ReadByte:result=c.position<c.wires[c.current].size()?static_cast<unsigned char>(c.wires[c.current][c.position++]):0xffffffffu;break;
    case HttpEffect::ReadBytes:{const auto size=std::min(action.size,c.wires[c.current].size()-c.position);
        std::memcpy(action.destination,c.wires[c.current].data()+c.position,size);c.position+=size;result=static_cast<std::uint32_t>(size);
        c.events.push_back("read_bytes|"+std::to_string(action.size)+"|"+std::string(reinterpret_cast<const char*>(action.destination),size));return true;}
    case HttpEffect::Connected:result=c.position<c.wires[c.current].size();break;
    case HttpEffect::Stop:break;
    case HttpEffect::SocketError:result=113;break;
    default:return false;
    }
    c.events.push_back("io|"+std::to_string(number)+"|"+std::to_string(result));return true;
}
}
extern "C" __attribute__((visibility("default"))) int http_backend_network_native(const char* input,unsigned missing,unsigned failure,char* output,std::size_t capacity){
    DynamicJsonDocument document(65536);if(deserializeJson(document,input))return 2;
    std::unique_ptr<Context> storage(new Context);auto& c=*storage;c.missing=missing;c.failure=failure;
    c.clock=document["clock"];c.counter=document["counter"];c.water=document["water"];c.factory=document["factory"];c.connect=document["connect"];c.yield_ms=document["yield_ms"];
    const auto wires=document["wires"].as<JsonArrayConst>();
    for(std::size_t i=0;i<wires.size();++i)c.wires.push_back(wires[i].as<std::string>());
    ConsoleServices services;services.context=&c;services.assertion=assertion;services.log=log;
    c.backend.diagnostics=services;c.backend.hooks.context=&c;
    c.backend.hooks.assertion=[](void* p,std::uint32_t line){auto& c=*static_cast<Context*>(p);return report_backend_assertion(c.backend.diagnostics,BackendDiagnosticDomain::Http,line);};
    c.backend.hooks.diagnostic=[](void* p,std::uint32_t line){auto& c=*static_cast<Context*>(p);return report_http_backend_diagnostic(c.backend.diagnostics,c.backend,line);};
    c.backend.environment=RecipeEnvironment(document["pressure"].as<double>(),document["maximum"].as<double>());
    c.backend.authorization=original_credentials::http_authorization;c.backend.boiler=static_cast<BoilerType>(2);c.event.source=static_cast<CoreCommandId>(document["source"].as<std::uint32_t>());
    const char* initial=document["same_uid"].as<bool>()?original_credentials::http_client_test_text(nullptr,0x3f418735):document["uid"].as<const char*>();
    if(!initial||std::strlen(initial)>12)return 3;
    std::strcpy(c.identifier.data(),initial);c.backend.token=c.identifier.data();
    c.client.name="HttpBackend.h";c.client.timing.name=c.client.name;c.client.services.context=&c;c.client.services.perform=perform;c.client.services.diagnostics=services;
    c.client.services.millis=[](void* p){return static_cast<Context*>(p)->clock;};
    ApplicationConsoleHooks hooks;hooks.context=&c;hooks.test_scope=scope;hooks.millis=c.client.services.millis;
    hooks.yield=[](void* p){auto& c=*static_cast<Context*>(p);c.events.push_back("yield");c.clock+=c.yield_ms;return c.failure!=4;};
    HttpClientTestResources resources;resources.text=[](void*,std::uint32_t address){return original_credentials::http_client_test_text(nullptr,address);};
    switch(missing){case 1:services.assertion=nullptr;break;case 2:services.log=nullptr;break;case 3:hooks.test_scope=nullptr;break;case 4:hooks.millis=nullptr;break;case 5:hooks.yield=nullptr;break;case 6:resources.text=nullptr;break;case 7:c.client.services.perform=nullptr;break;case 8:c.backend.authorization=nullptr;break;default:break;}
    const bool result=http_backend_network_console_test(c.backend,c.identifier,services,hooks,resources);
    DynamicJsonDocument response(4000000);response["result"]=result;auto state=response.createNestedArray("state");
    for(auto value:{c.counter,c.clock,c.backend.state,static_cast<std::uint32_t>(c.state.ready),static_cast<std::uint32_t>(c.state.error),
            static_cast<std::uint32_t>(c.state.registered),static_cast<std::uint32_t>(c.state.token_expired),static_cast<std::uint32_t>(c.state.backend_error),
            static_cast<std::uint32_t>(c.state.resumable_session_id),static_cast<std::uint32_t>(c.event.type),static_cast<std::uint32_t>(c.event.source),
            c.event.count,c.event.offset,c.event.total,static_cast<std::uint32_t>(c.event.byte_ddc),c.backend.pending_recipe.id,
            static_cast<std::uint32_t>(c.backend.pending_recipe.kind),static_cast<std::uint32_t>(c.backend.reference_id),static_cast<std::uint32_t>(c.client.state),
            static_cast<std::uint32_t>(c.client.error),static_cast<std::uint32_t>(c.requests),static_cast<std::uint32_t>(c.position),
            static_cast<std::uint32_t>(c.mailbox.pending),static_cast<std::uint32_t>(c.mailbox.value.type)})state.add(value);
    response["identifier"]=std::string(c.identifier.data());auto events=response.createNestedArray("events");for(const auto& event:c.events)events.add(event);
    if(response.overflowed()||measureJson(response)+1>capacity)return 4;
    serializeJson(response,output,capacity);return 0;
}

#ifdef HTTP_BACKEND_NETWORK_SANITIZER_MAIN
#include <iostream>
int main(){
    std::unique_ptr<char[]> output(new char[4000000]);std::string input;unsigned count=0;
    while(std::getline(std::cin,input)){
        if(http_backend_network_native(input.c_str(),0,0,output.get(),4000000)||
           !std::strstr(output.get(),"\"result\":true"))return 1;
        if(count==0){
            for(unsigned missing=1;missing<=8;++missing)
                if(http_backend_network_native(input.c_str(),missing,0,output.get(),4000000)||
                   !std::strstr(output.get(),"\"result\":false"))return 2;
            for(unsigned failure=1;failure<=6;++failure)
                if(http_backend_network_native(input.c_str(),0,failure,output.get(),4000000)||
                   !std::strstr(output.get(),"\"result\":false"))return 3;
        }
        ++count;
    }
    return count==12?0:4;
}
#endif
