#include "zseries/application_console.hpp"
#include "zseries/core_application.hpp"
#include <ArduinoJson.h>
#include <iostream>
#include <memory>
#include <string>
using namespace zseries::oem;
struct Capture {JsonArray events;JsonArrayConst reply;std::size_t at=0,returned=0;std::uint32_t clock=0,count=0;};
int main(){std::string line;while(std::getline(std::cin,line)){
    DynamicJsonDocument input(2048),output(32768);if(deserializeJson(input,line))return 2;
    CoreEventMailbox mailbox;std::unique_ptr<CoreApplication> owned(new CoreApplication(mailbox));auto& app=*owned;
    Capture c;c.events=output.createNestedArray("events");c.reply=input["bytes"].as<JsonArrayConst>();c.returned=input["returned"].as<unsigned>();c.clock=input["clock"].as<std::uint32_t>();c.count=input["counter"].as<std::uint32_t>();
    ConsoleServices d;d.context=&c;
    d.log=[](void* p,ConsoleLevel level,const char* tag,const char* file,std::uint32_t source,const char* message){
        static_cast<Capture*>(p)->events.add("log|"+std::to_string(unsigned(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(source)+"|"+message);return true;};
    d.assertion=[](void* p,const char* file,std::uint32_t source,const char* expression){auto& c=*static_cast<Capture*>(p);
        c.events.add("assertion|"+std::string(file)+"|"+std::to_string(source)+"|"+expression);++c.count;c.clock+=200;return true;};
    app.ports.context=&c;app.ports.millis=[](void* p){return static_cast<Capture*>(p)->clock;};app.ports.diagnostics=d;
    auto& bus=app.ports.sensor_bus;bus.context=&c;
    bus.request_from=[](void* p,std::uint8_t address,std::size_t count,bool stop){auto& c=*static_cast<Capture*>(p);
        c.events.add("request|"+std::to_string(address)+"|"+std::to_string(count)+"|"+(stop?"1":"0"));return c.returned;};
    bus.read=[](void* p){auto& c=*static_cast<Capture*>(p);const int value=c.reply[c.at++].as<int>();c.events.add("read|"+std::to_string(value));return value;};
    for(unsigned i=0;i<4;++i){app.acquisition.adc.raw[i]=input["raw"][i].as<std::int32_t>();app.acquisition.adc.sensor_types[i]=static_cast<TempSensorType>(input["types"][i].as<unsigned>());}
    app.acquisition.adc.polling_enabled=input["enabled"].as<bool>();
    ApplicationConsole console(app,d);if(console.register_max11613()!=ConsoleResult::Ok||console.register_component(CoreSetupStage::Diagnostics)!=ConsoleResult::Ok)return 3;
    const unsigned op=input["operation"].as<unsigned>();const auto result=console.dispatch(op==0?"max i":op==1?"max read":"max auto");
    if(result!=ConsoleResult::Ok){std::cerr<<"Console failed: "<<input["id"].as<std::string>()<<" result="<<unsigned(result)<<"\n";return 4;}
    auto state=output.createNestedArray("state");state.add(c.count);state.add(c.clock);for(auto raw:app.acquisition.adc.raw)state.add(raw);
    serializeJson(output,std::cout);std::cout<<'\n';
}}
