#include "zseries/network_console_tests.hpp"
#include <cstring>
#include <string>
using namespace zseries::oem;
namespace {
struct Context {
    std::uint32_t count,clock,network,ethernet,wifi,available,kind,delay,assertions,index;
    std::string events;
};
bool assertion(void* opaque,const char* file,std::uint32_t line,const char* expression){auto& c=*static_cast<Context*>(opaque);
    c.events+="assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression+"\n";++c.count;c.clock+=200;return true;}
bool log(void* opaque,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){auto& c=*static_cast<Context*>(opaque);
    c.events+="log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(line)+"|"+message+"\n";return true;}
bool message(void* opaque,const char* text){static_cast<Context*>(opaque)->events+="message|"+std::string(text)+"\n";return true;}
bool write(void* opaque,const char* bytes,std::size_t size){auto& output=static_cast<Context*>(opaque)->events;const char* hex="0123456789abcdef";output+="write|";
    for(std::size_t i=0;i<size;++i){const auto value=static_cast<unsigned char>(bytes[i]);output+=hex[value>>4];output+=hex[value&15];}output+='\n';return true;}
bool trace(void* opaque,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){auto& c=*static_cast<Context*>(opaque);
    c.events+="trace|"+std::to_string(begin?1:0)+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+name+"\n";return true;}
bool child(void* opaque,std::uint32_t callback,const char*){auto& c=*static_cast<Context*>(opaque);c.events+="invoke|"+std::to_string(callback)+"\n";
    c.clock+=c.delay;if(c.assertions&(1u<<c.index))assertion(opaque,"Child.h",9,"prescribed child assertion");++c.index;return true;}
bool http(void* opaque,const HttpAction& action,std::uint32_t& result){auto& c=*static_cast<Context*>(opaque);
    if(action.effect!=HttpEffect::AcquireClient||!action.acquired_class)return false;
    *action.acquired_class=static_cast<HttpClientClass>(c.kind);result=c.available;
    c.events+="acquire|"+std::to_string(action.argument)+"|"+std::to_string(c.available)+"|"+std::to_string(c.kind)+"\n";return true;}
void bind(Context& c,ConsoleServices& s,ApplicationConsoleHooks& h,HttpServices& transport,NetworkConsolePorts& ports){
    s.context=&c;s.assertion=assertion;s.log=log;h.context=&c;h.invoke=child;h.scope=trace;
    h.millis=[](void* p){return static_cast<Context*>(p)->clock;};h.assertion_count=[](void* p){return static_cast<Context*>(p)->count;};
    h.test_message=message;h.write=write;transport.context=&c;transport.perform=http;h.http_test_services=&transport;
    ports.context=&c;ports.selected_network=&c.network;
    ports.ethernet_address=[](void* p,std::uint32_t& value){auto& c=*static_cast<Context*>(p);value=c.ethernet;c.events+="address|Ethernet|"+std::to_string(value)+"\n";return true;};
    ports.wifi_address=[](void* p,std::uint32_t& value){auto& c=*static_cast<Context*>(p);value=c.wifi;c.events+="address|WiFi|"+std::to_string(value)+"\n";return true;};
}
}
extern "C" __attribute__((visibility("default"))) unsigned network_console_native(const std::uint32_t* args,std::uint32_t* out,char* events){
    Context c{args[2],args[1],args[3],args[4],args[5],args[6],args[7],args[8],args[9],0,{}};
    ConsoleServices services;ApplicationConsoleHooks hooks;HttpServices transport;NetworkConsolePorts ports;bind(c,services,hooks,transport,ports);
    bool result=false;switch(args[0]){
    case 0:result=binary_download_console_test(transport,services,hooks);break;
    case 1:case 2:result=core_network_console_self_test(ports,services,hooks);break;
    case 3:case 4:result=core_integration_console_self_test(services,hooks);break;
    case 5:result=core_all_console_self_test(services,hooks);break;
    default:return 98;}
    if(c.events.size()>=32768)return 99;
    std::memcpy(events,c.events.c_str(),c.events.size()+1);out[0]=c.count;out[1]=c.clock;out[2]=c.network;return result?0:1;
}
extern "C" __attribute__((visibility("default"))) unsigned network_console_failures(){
    Context c{0,1000,7,1,2,1,1,0,0,0,{}};ConsoleServices s;ApplicationConsoleHooks h;HttpServices transport;NetworkConsolePorts ports;
    bind(c,s,h,transport,ports);unsigned result=0;
    auto broken=h;broken.invoke=nullptr;
    if(!core_network_console_self_test(ports,s,broken)&&!core_integration_console_self_test(s,broken)&&!core_all_console_self_test(s,broken))result|=1;
    broken=h;broken.invoke=[](void*,std::uint32_t,const char*){return false;};
    if(!core_network_console_self_test(ports,s,broken)&&!core_integration_console_self_test(s,broken)&&!core_all_console_self_test(s,broken))result|=2;
    broken=h;broken.http_test_services=nullptr;if(!core_network_console_self_test(ports,s,broken))result|=4;
    auto bad=transport;bad.perform=nullptr;if(!binary_download_console_test(bad,s,h))result|=8;
    bad=transport;bad.perform=[](void*,const HttpAction&,std::uint32_t&){return false;};
    broken=h;broken.http_test_services=&bad;
    if(!core_network_console_self_test(ports,s,broken)&&!binary_download_console_test(bad,s,h))result|=16;
    broken=h;broken.scope=nullptr;if(!binary_download_console_test(transport,s,broken))result|=32;
    auto missing=ports;missing.ethernet_address=nullptr;if(!core_network_console_self_test(missing,s,h))result|=64;
    auto failed=s;failed.assertion=[](void*,const char*,std::uint32_t,const char*){return false;};c.ethernet=c.wifi=0;
    if(!core_network_console_self_test(ports,failed,h))result|=128;
    return result;
}
