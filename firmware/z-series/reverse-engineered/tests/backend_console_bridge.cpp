#include "zseries/backend_console_tests.hpp"
#include "zseries/console_test_support.hpp"
#include "zseries/backend_log_event.hpp"
#include <cstring>
#include <string>
using namespace zseries::oem;
namespace {
struct Context{std::uint32_t count,clock,water;std::string events;};
bool assertion(void* p,const char* file,std::uint32_t line,const char* expression){auto& c=*static_cast<Context*>(p);c.events+="assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression+"\n";++c.count;c.clock+=200;return true;}
bool log(void* p,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){static_cast<Context*>(p)->events+="log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(line)+"|"+message+"\n";return true;}
bool test_scope(void* p,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){auto& c=*static_cast<Context*>(p);c.events+="scope|"+std::to_string(begin?1:0)+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+name+"\n";if(!begin&&c.water<=700)assertion(p,"Assert.h",0x48,"stackHighWaterMark > 700");return true;}
std::uint32_t millis(void* p){return static_cast<Context*>(p)->clock;}
std::uint32_t counter(void* p){return static_cast<Context*>(p)->count;}
bool message(void* p,const char* text){static_cast<Context*>(p)->events+="message|"+std::string(text)+"\n";return true;}
bool write(void* p,const char* text,std::size_t size){auto& out=static_cast<Context*>(p)->events;out+="write|";const char* hex="0123456789abcdef";for(std::size_t i=0;i<size;++i){const auto c=static_cast<unsigned char>(text[i]);out+=hex[c>>4];out+=hex[c&15];}out+='\n';return true;}
}
extern "C" __attribute__((visibility("default"))) unsigned backend_console_test_native(const std::uint32_t* args,std::uint32_t* out,char* events){
    Context c{args[2],args[1],args[3],{}};ConsoleServices services;services.context=&c;services.assertion=assertion;services.log=log;
    ApplicationConsoleHooks hooks;hooks.context=&c;hooks.test_scope=test_scope;hooks.millis=millis;hooks.assertion_count=counter;hooks.test_message=message;hooks.write=write;
    bool result=false;
    if(args[0]==0)result=http_backend_console_self_test(services,hooks);
    else if(args[0]==1)result=z_backend_console_self_test(services,hooks);
    else if(args[0]==2){const char* names[]={"Basic Tests","Network Tests","Int Tests","All Tests",""};if(args[4]>4)return 98;ConsoleTestMarker marker(services,hooks,names[args[4]]);result=marker.begin();if(result){c.clock+=args[5];c.count+=args[6];result=marker.end();}}
    else if(args[0]==3){BackendLogEvent event;result=backend_log_msp_error(event,args[4],services)==ConsoleResult::Ok;out[2]=static_cast<std::uint32_t>(event.type);}
    else if(args[0]==4){BackendLogEvent event;SafetyState safety;safety.max_hex_wort_delta=(args[4]&1)!=0;safety.max_wort_drain_delta=(args[4]&2)!=0;safety.max_hex_rise=(args[4]&4)!=0;safety.filter_missing=(args[4]&8)!=0;safety.thermal.too_hot=args[5]&255;safety.thermal.too_cold=(args[5]>>8)&255;safety.thermal.too_old=(args[5]>>16)&255;result=backend_log_safety_error(event,safety,services)==ConsoleResult::Ok;out[2]=static_cast<std::uint32_t>(event.type);}
    if(c.events.size()>=131072)return 99;
    std::memcpy(events,c.events.c_str(),c.events.size()+1);out[0]=c.count;out[1]=c.clock;return result?0:1;
}
extern "C" __attribute__((visibility("default"))) unsigned backend_console_service_failures(){
    Context c{0,1000,4096,{}};ConsoleServices s;s.context=&c;s.assertion=assertion;s.log=log;
    ApplicationConsoleHooks h;h.context=&c;h.test_scope=test_scope;h.millis=millis;h.assertion_count=counter;h.test_message=message;h.write=write;
    unsigned passed=0;
    for(unsigned i=0;i<3;++i){auto broken=s;auto hooks=h;if(i==0)broken.assertion=nullptr;else if(i==1)broken.log=nullptr;else hooks.test_scope=nullptr;
        if(!http_backend_console_self_test(broken,hooks)&&!z_backend_console_self_test(broken,hooks))passed|=1u<<i;
    }
    auto failed=s;failed.log=[](void*,ConsoleLevel,const char*,const char*,std::uint32_t,const char*){return false;};
    if(!http_backend_console_self_test(failed,h)&&!z_backend_console_self_test(failed,h))passed|=8;
    auto hooks=h;hooks.test_scope=[](void*,bool,const char*,const char*,std::uint32_t,const char*){return false;};
    if(!http_backend_console_self_test(s,hooks)&&!z_backend_console_self_test(s,hooks))passed|=16;
    bool all=true;
    for(unsigned i=0;i<6;++i){auto broken=s;hooks=h;switch(i){case 0:hooks.millis=nullptr;break;case 1:hooks.assertion_count=nullptr;break;case 2:hooks.test_message=nullptr;break;case 3:hooks.write=nullptr;break;case 4:broken.assertion=nullptr;break;default:broken.log=nullptr;break;}
        ConsoleTestMarker marker(broken,hooks,"Basic Tests");all=all&&!marker.begin();}
    if(all)passed|=32;
    hooks=h;hooks.write=[](void*,const char*,std::size_t){return false;};
    {ConsoleTestMarker marker(s,hooks,"Basic Tests");if(marker.begin()&&!marker.end())passed|=64;}
    BackendLogEvent event;ConsoleServices missing;
    if(backend_log_msp_error(event,1,missing)==ConsoleResult::MissingService&&
       backend_log_msp_error(event,1,failed)==ConsoleResult::ServiceFailure)passed|=128;
    return passed;
}
#ifdef BACKEND_CONSOLE_SANITIZER_MAIN
#include <memory>
int main(){
    std::unique_ptr<char[]> events(new char[131072]);std::uint32_t result[3]={};
    for(unsigned operation=0;operation<5;++operation){std::uint32_t args[]={operation,0xffffff50u,0xfffffffau,700,operation==2?4u:15u,1234567,2};
        if(backend_console_test_native(args,result,events.get()))return 1;}
    return backend_console_service_failures()==255?0:2;
}
#endif
