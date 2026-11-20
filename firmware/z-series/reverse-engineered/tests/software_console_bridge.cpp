#include "zseries/software_console_tests.hpp"
#include <cstring>
#include <string>
using namespace zseries::oem;
namespace {
struct Context{std::uint32_t count,clock,water;std::string events;};
bool assertion(void*p,const char*file,std::uint32_t line,const char*expression){auto&c=*static_cast<Context*>(p);c.events+="assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression+"\n";++c.count;c.clock+=200;return true;}
bool log(void*p,ConsoleLevel level,const char*tag,const char*file,std::uint32_t line,const char*message){static_cast<Context*>(p)->events+="log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(line)+"|"+message+"\n";return true;}
bool scope(void*p,bool begin,const char*tag,const char*file,std::uint32_t line,const char*name){auto&c=*static_cast<Context*>(p);c.events+="scope|"+std::to_string(begin?1:0)+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+name+"\n";if(!begin&&c.water<=700)assertion(p,"Assert.h",0x48,"stackHighWaterMark > 700");return true;}
std::uint32_t millis(void*p){return static_cast<Context*>(p)->clock;}
}
extern "C" __attribute__((visibility("default"))) unsigned software_console_test_native(const std::uint32_t* args,std::uint32_t* out,char*events){
 Context c{args[2],args[1],args[3],{}};ConsoleServices services;services.context=&c;services.assertion=assertion;services.log=log;ApplicationConsoleHooks hooks;hooks.context=&c;hooks.test_scope=scope;SoftwareConsoleClock clock;clock.context=&c;clock.millis=millis;
 bool result=false;
 switch(args[0]){case 0:result=runtime_console_self_test(services,hooks,clock);break;case 1:result=still_sensors_console_self_test(services,hooks);break;case 2:result=still_state_console_self_test(services,hooks);break;case 3:result=still_parser_console_self_test(services,hooks);break;case 4:case 6:result=still_backend_console_self_test(services,hooks);break;case 5:result=still_console_self_test(services,hooks);break;}
 if(c.events.size()>=32768)return 99;
 std::memcpy(events,c.events.c_str(),c.events.size()+1);out[0]=c.count;out[1]=c.clock;return result?0:1;
}
extern "C" __attribute__((visibility("default"))) unsigned software_console_missing_services(){ConsoleServices services;ApplicationConsoleHooks hooks;SoftwareConsoleClock clock;return (!runtime_console_self_test(services,hooks,clock)?1u:0u)|(!still_sensors_console_self_test(services,hooks)?2u:0u)|(!still_state_console_self_test(services,hooks)?4u:0u)|(!still_parser_console_self_test(services,hooks)?8u:0u)|(!still_backend_console_self_test(services,hooks)?16u:0u)|(!still_console_self_test(services,hooks)?32u:0u);}
