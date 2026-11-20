#include "zseries/application_console.hpp"
#include "zseries/core_application.hpp"
#include <ArduinoJson.h>
#include <iostream>
#include <memory>
#include <string>
#include <cstring>
using namespace zseries::oem;
struct Capture {JsonArray effects;std::uint32_t clock=1000,counter=7,ethernet=0,wifi=0,available=0,client_class=0;std::uint8_t bsl_response=0;bool platform=true;};
static const ConsoleEntry* find_entry(const ConsoleShell& root,const char* shell,const char* token) {
    for(std::size_t i=0;i<root.size();++i){const auto* entry=root.entry(i);
        if(!std::strcmp(root.name(),shell)&&!std::strcmp(entry->token,token))return entry;
        if(entry->child){const auto* found=find_entry(*entry->child,shell,token);if(found)return found;}}
    return nullptr;
}
static void tree(JsonArray result,const ConsoleShell& shell) {
    auto node=result.createNestedObject();node["name"]=shell.name();node["capacity"]=shell.capacity();auto entries=node.createNestedArray("entries");
    for(std::size_t i=0;i<shell.size();++i){const auto& value=*shell.entry(i);auto entry=entries.createNestedObject();entry["token"]=value.token;
        if(value.child)entry["child"]=value.child->name();else{entry["exact"]=value.exact_match;entry["description"]=value.description;}}
    for(std::size_t i=0;i<shell.size();++i)if(shell.entry(i)->child)tree(result,*shell.entry(i)->child);
}
int main() {
    std::string line;
    while(std::getline(std::cin,line)) {
        DynamicJsonDocument input(4096);if(deserializeJson(input,line))return 2;
        DynamicJsonDocument output(input["diagnostic_aggregate"].as<bool>()?2000000:500000);
        CoreEventMailbox head;std::unique_ptr<CoreApplication> owned(new CoreApplication(head));auto& app=*owned;
        Capture capture;capture.effects=output.createNestedArray("effects");capture.platform=!input["missing_platform"].as<bool>();
        capture.bsl_response=input["bsl_response"]|0;
        capture.ethernet=input["ethernet"]|0u;capture.wifi=input["wifi"]|0u;
        capture.available=input["available"]|0u;capture.client_class=input["client_class"]|0u;
        ConsoleServices services;services.context=&capture;
        services.assertion=[](void* context,const char* file,std::uint32_t source,const char* expression){auto& c=*static_cast<Capture*>(context);auto e=c.effects.createNestedArray();e.add("assertion");e.add(std::string(file));e.add(source);e.add(std::string(expression));++c.counter;c.clock+=200;return true;};
        services.log=[](void* context,ConsoleLevel level,const char* tag,const char* file,std::uint32_t source,const char* message){
            auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("log");e.add(static_cast<unsigned>(level));e.add(std::string(tag));e.add(std::string(file?file:""));e.add(source);e.add(std::string(message));return true;};
        app.ports.diagnostics=services;app.ports.context=&capture;app.ports.millis=[](void* context){return static_cast<Capture*>(context)->clock;};
        app.ports.heater_gpio.context=&capture;
        app.ports.heater_gpio.write_pin=[](void* context,std::uint8_t pin,bool value){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("gpio");e.add(pin);e.add(value);};
        app.ports.heater_gpio.configure_output=[](void*,std::uint8_t){};
        app.encoder.io.context=&capture;app.encoder.io.millis=[](void* context){return static_cast<Capture*>(context)->clock;};
        app.encoder.services.context=&capture;app.encoder.services.rotation_event=[](void* context,std::int32_t delta){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("encoder");e.add(delta);return true;};
        app.heater.pid.automatic=input["automatic"].as<bool>();app.heater.state.started=input["started"].as<bool>();
        app.heater.state.output_ticks=input["duty"]|0.0;app.temperatures.estimated_boil_celsius=100;
        if(input.containsKey("ssid"))std::strncpy(app.wifi.state.requested.ssid.data(),input["ssid"].as<const char*>(),app.wifi.state.requested.ssid.size()-1);
        app.safeties.thermal.enabled=input["safety_enabled"]|true;
        app.safety_controller.hex_rise.enabled=app.safety_controller.hex_wort_delta.enabled=app.safety_controller.wort_drain_delta.enabled=true;
        app.safeties.max_hex_rise=app.safeties.max_hex_wort_delta=app.safeties.max_wort_drain_delta=true;
        app.msp_state.id=input["msp_state"]|2;app.msp_state.pumps=input["pumps"]|0x50;app.msp.state=&app.msp_state;app.msp.address=0x40;
        app.msp.transport.context=&capture;app.msp.transport.transmit=[](void* context,std::uint8_t address,const std::uint8_t* bytes,std::size_t size){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("msp_tx");e.add(address);auto data=e.createNestedArray();for(std::size_t i=0;i<size;++i)data.add(bytes[i]);};
        app.msp.transport.receive=[](void* context,std::uint8_t,std::uint8_t* bytes,std::size_t size){std::memset(bytes,0,size);if(size==1)bytes[0]=static_cast<Capture*>(context)->bsl_response;};
        app.msp.transport.delay_ms=[](void* context,std::uint32_t delay){auto& c=*static_cast<Capture*>(context);auto e=c.effects.createNestedArray();e.add("delay");e.add(delay);c.clock+=delay;};
        app.still.state.id=static_cast<PicoStillStateId>(input["still_state"]|1);
        app.still_client.services.context=&capture;app.still_client.services.perform=[](void* context,const StillHttpAction& action,std::uint32_t& accepted){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("still_http");e.add(static_cast<unsigned>(action.effect));e.add(action.uri?action.uri:"");accepted=1;return true;};
        app.runtime.state.t1_drop_watch.enabled=input["t1_enabled"].as<bool>();
        if(input["occupied"].as<bool>()){CoreCommand c;c.id=CoreCommandId::StandBy;app.post(c);RuntimeCommand r;r.id=RuntimeCommandId::SetDuration;r.duration=33;app.post_runtime(r);PicoStillCommand s;s.id=StillCommandId::Reconnect;app.still.commands.put(s);}
        ApplicationConsole console(app,services);console.hooks.context=&capture;
        if(capture.platform)console.hooks.invoke=[](void* context,std::uint32_t callback,const char* argument){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("platform");e.add(callback);e.add(argument?argument:"");return true;};
        console.hooks.write=[](void* context,const char* bytes,std::size_t size){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("write");e.add(std::string(bytes,size));return true;};
        console.hooks.scope=[](void* context,bool begin,const char* tag,const char* file,std::uint32_t source,const char* name){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("scope");e.add(begin);e.add(std::string(tag));e.add(std::string(file));e.add(source);e.add(std::string(name));return true;};
        console.hooks.test_scope=console.hooks.scope;
        console.hooks.millis=[](void* context){return static_cast<Capture*>(context)->clock;};
        console.hooks.assertion_count=[](void* context){return static_cast<Capture*>(context)->counter;};
        console.hooks.stack_high_water=[](void*){return 4096u;};
        console.hooks.yield=[](void*){return true;};
        console.hooks.delay_ms=[](void* context,std::uint32_t ms){auto& c=*static_cast<Capture*>(context);c.clock+=ms;auto e=c.effects.createNestedArray();e.add("delay");e.add(ms);return true;};
        console.hooks.test_message=[](void* context,const char* text){auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("message");e.add(std::string(text));return true;};
        HttpServices test_transport;
        if(input["network_diagnostic"].as<bool>()){
            app.network=input["network"]|7u;
            app.ports.ethernet_address=[](void* context){auto& c=*static_cast<Capture*>(context);auto e=c.effects.createNestedArray();e.add("address");e.add("Ethernet");e.add(c.ethernet);return c.ethernet;};
            app.wifi.services.context=&capture;
            app.wifi.services.perform=[](void* context,const WifiAction& action,std::uint32_t& result){auto& c=*static_cast<Capture*>(context);if(action.effect!=WifiEffect::LocalAddress||!action.address)return false;*action.address=c.wifi;result=1;auto e=c.effects.createNestedArray();e.add("address");e.add("WiFi");e.add(c.wifi);return true;};
            test_transport.context=&capture;
            test_transport.perform=[](void* context,const HttpAction& action,std::uint32_t& result){auto& c=*static_cast<Capture*>(context);if(action.effect!=HttpEffect::AcquireClient||!action.acquired_class)return false;*action.acquired_class=static_cast<HttpClientClass>(c.client_class);result=c.available;auto e=c.effects.createNestedArray();e.add("acquire");e.add(action.argument);e.add(c.available);e.add(c.client_class);return true;};
            console.hooks.http_test_services=&test_transport;
        }
        if(input["diagnostic_aggregate"].as<bool>())console.hooks.scope=[](void* context,bool begin,const char* tag,const char* file,std::uint32_t source,const char* name){
            auto e=static_cast<Capture*>(context)->effects.createNestedArray();e.add("log");e.add(0);e.add(std::string(tag));e.add(std::string(file));e.add(source);e.add(std::string(name)+(begin?" BEGIN 0":" END   0"));return true;};
        bool registered=true;
        for(unsigned i=0;i<=static_cast<unsigned>(CoreSetupStage::Diagnostics);++i)registered=registered&&console.register_component(static_cast<CoreSetupStage>(i))==ConsoleResult::Ok;
        registered=registered&&console.register_display()==ConsoleResult::Ok&&console.register_heater()==ConsoleResult::Ok&&console.register_max11613()==ConsoleResult::Ok;
        app.eeprom.services=console.eeprom_services();output["registered"]=registered;
        if((input["op"]|std::string())=="registry"){
            tree(output.createNestedArray("shells"),*console.root());
            const auto size=console.root()->size();console.register_component(CoreSetupStage::Diagnostics);output["idempotent"]=console.root()->size()==size;
        }else{
            if(input.containsKey("direct_callback"))output["result"]=static_cast<unsigned>(console.invoke_original(input["direct_callback"].as<std::uint32_t>(),input["direct_argument"]|""));
            else if(input.containsKey("direct_argument")){
                const auto* entry=find_entry(*console.root(),input["direct_shell"]|"",input["direct_token"]|"");
                output["result"]=static_cast<unsigned>(entry&&entry->invoke&&entry->invoke(entry->context,input["direct_argument"]|"")?ConsoleResult::Ok:ConsoleResult::ServiceFailure);
            }else output["result"]=static_cast<unsigned>(console.dispatch(input["command"]|""));
            auto state=output.createNestedObject("state");state["clock"]=capture.clock;state["counter"]=capture.counter;
            auto wifi=state.createNestedArray("wifi");wifi.add(static_cast<unsigned>(app.wifi.target));wifi.add(app.wifi.state.requested.ssid.data());wifi.add(app.wifi.state.requested.password.data());wifi.add(app.wifi.state.requested.internet_expected);
            auto heater=state.createNestedArray("heater");heater.add(app.heater.state.started);heater.add(app.heater.state.enabled);heater.add(app.heater.pid.automatic);heater.add(app.heater.state.output_ticks);heater.add(app.heater.state.target_celsius);heater.add(app.heater.state.target_excess);
            auto safety=state.createNestedArray("safety");safety.add(app.safeties.thermal.enabled);safety.add(app.safeties.filter_missing);safety.add(app.safeties.max_hex_rise);safety.add(app.safeties.max_hex_wort_delta);safety.add(app.safeties.max_wort_drain_delta);
            auto runtime=state.createNestedArray("runtime");runtime.add(app.runtime_commands.pending);runtime.add(static_cast<unsigned>(app.runtime_commands.value.id));runtime.add(app.runtime_commands.value.program_id);runtime.add(app.runtime_commands.value.temperature);runtime.add(app.runtime.state.t1_drop_watch.enabled);runtime.add(app.runtime.state.t1_drop_watch.threshold);
            std::uint64_t temperature_bits;std::memcpy(&temperature_bits,&app.runtime_commands.value.temperature,sizeof(temperature_bits));state["runtime_temperature_bits"]=std::to_string(temperature_bits);
            auto still=state.createNestedArray("still");still.add(app.still.commands.pending);still.add(static_cast<unsigned>(app.still.commands.value.id));still.add(app.still.commands.value.selection);
            auto core=state.createNestedArray("core");core.add(app.core.commands.pending);core.add(static_cast<unsigned>(app.core.commands.value.id));
            state["stop_requested"]=app.session_logger.stop_requested;state["adc"]=app.acquisition.adc.polling_enabled;state["moving"]=app.msp_state.moving_valve;
            state["network"]=app.network;
        }
        std::string serialized;serializeJson(output,serialized);const char* hex="0123456789abcdef";
        for(unsigned char byte:serialized){if(byte<32)std::cout<<"\\u00"<<hex[byte>>4]<<hex[byte&15];else std::cout<<byte;}std::cout<<'\n';
    }
}
