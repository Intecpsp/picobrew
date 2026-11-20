#include "zseries/network_console_tests.hpp"
#include "zseries/console_test_support.hpp"
#include "zseries/core_application.hpp"
#include <cstdio>

namespace zseries { namespace oem {
namespace {
bool invoke(const ApplicationConsoleHooks& hooks,std::uint32_t callback){
    return hooks.invoke&&hooks.invoke(hooks.context,callback,"");
}
bool require(const ConsoleServices& s,bool condition,std::uint32_t line,const char* expression){
    return condition||(s.assertion&&s.assertion(s.context,"Core.h",line,expression));
}
bool log(const ConsoleServices& s,ConsoleLevel level,std::uint32_t line,const char* message){
    return s.log&&s.log(s.context,level,"CORE","Core.h",line,message);
}
}
bool core_network_console_self_test(const NetworkConsolePorts& ports,const ConsoleServices& s,
        const ApplicationConsoleHooks& hooks){
    ConsoleTestMarker marker(s,hooks,"Network Tests");if(!marker.begin())return false;
    if(!ports.ethernet_address||!ports.wifi_address||!ports.selected_network)return false;
    std::uint32_t address=0,selected=1;
    if(!ports.ethernet_address(ports.context,address))return false;
    if(!address){selected=2;if(!ports.wifi_address(ports.context,address))return false;
        if(!address){
            if(!require(s,false,0x22e,"netType != NetworkType::None")||
               !log(s,ConsoleLevel::Error,0x231,"Network test cannot run without network"))return false;
            return marker.end();
        }
    }
    char message[80];::snprintf(message,sizeof message,"Network test using %s",selected==1?"Ethernet":"WiFi");
    if(!log(s,ConsoleLevel::Debug,0x235,message))return false;
    *ports.selected_network=selected;
    if(!hooks.http_test_services||!hooks.http_test_services->perform)return false;
    HttpClientClass kind=HttpClientClass::None;HttpAction action(HttpEffect::AcquireClient);action.acquired_class=&kind;
    std::uint32_t available=0;
    if(!hooks.http_test_services->perform(hooks.http_test_services->context,action,available))return false;
    // The concrete adapter's result is accepted && client != nullptr. Those
    // values coincide for the original factory's defined return paths.
    if(!require(s,available!=0,0x23c,"res")||!require(s,available!=0,0x23d,"c != nullptr")||
       !require(s,kind!=HttpClientClass::None,0x23e,"cc != ClientClass::None"))return false;
    const char* names[]={"Azure","DownloadBinary","Pinger","HttpClient","HttpBackend"};
    const std::uint32_t callbacks[]={0x400fec74,0x401024a8,0x400f7204,0x40110f80,0x4011b6cc};
    for(unsigned i=0;i<5;++i){::snprintf(message,sizeof message,"TESTS %s::tests()",names[i]);
        if(!log(s,ConsoleLevel::Debug,0x240+i,message)||!invoke(hooks,callbacks[i]))return false;}
    return marker.end();
}
bool core_integration_console_self_test(const ConsoleServices& s,const ApplicationConsoleHooks& hooks){
    ConsoleTestMarker marker(s,hooks,"Int Tests");
    return marker.begin()&&log(s,ConsoleLevel::Warning,0x24a,"Running integration tests that will modify the system state!")&&
        invoke(hooks,0x400dd278)&&marker.end();
}
bool core_all_console_self_test(const ConsoleServices& s,const ApplicationConsoleHooks& hooks){
    ConsoleTestMarker marker(s,hooks,"All Tests");
    return marker.begin()&&invoke(hooks,0x4011ed44)&&invoke(hooks,0x4010a364)&&invoke(hooks,0x4011bffc)&&marker.end();
}
bool core_console_test(std::uint32_t callback,CoreApplication& app,const ConsoleServices& s,
        const ApplicationConsoleHooks& hooks){
    switch(callback){
    case 0x4011bffc:case 0x4011c2f8:{
        NetworkConsolePorts ports;ports.context=&app;ports.selected_network=&app.network;
        ports.ethernet_address=[](void* context,std::uint32_t& result){auto& owner=*static_cast<CoreApplication*>(context);
            if(!owner.ports.ethernet_address)return false;
            result=owner.ports.ethernet_address(owner.ports.context);return true;};
        ports.wifi_address=[](void* context,std::uint32_t& result){auto& owner=*static_cast<CoreApplication*>(context);
            if(!owner.wifi.services.perform)return false;
            WifiAction action(WifiEffect::LocalAddress);action.address=&result;std::uint32_t accepted=0;
            return owner.wifi.services.perform(owner.wifi.services.context,action,accepted);};
        return core_network_console_self_test(ports,s,hooks);}
    case 0x4010a364:case 0x4010a3d8:return core_integration_console_self_test(s,hooks);
    case 0x4011fbbc:return core_all_console_self_test(s,hooks);
    default:return false;
    }
}
}} // namespace zseries::oem
