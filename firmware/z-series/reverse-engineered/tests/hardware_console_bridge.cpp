#include "zseries/hardware_console_tests.hpp"
#include "zseries/application_console.hpp"
#include <cstring>
#include <string>
using namespace zseries::oem;
namespace {
struct Context{std::uint32_t count,line,clock,water;std::string events;};
bool assertion(void*p,const char*file,std::uint32_t line,const char*expression){auto&c=*static_cast<Context*>(p);c.events+="assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression+"\n";c.line=line;++c.count;c.clock+=200;return true;}
bool log(void*p,ConsoleLevel level,const char*tag,const char*file,std::uint32_t line,const char*message){static_cast<Context*>(p)->events+="log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+message+"\n";return true;}
bool scope(void*p,bool begin,const char*tag,const char*file,std::uint32_t line,const char*name){auto&c=*static_cast<Context*>(p);c.events+="scope|"+std::to_string(begin?1:0)+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+name+"\n";if(!begin&&c.water<=700)assertion(p,"Assert.h",0x48,"stackHighWaterMark > 700");return true;}
}
extern "C" unsigned hardware_console_test_native(const std::uint32_t* args,std::uint32_t* out,char*events){
 Context c{args[2],0,args[1],args[3],{}};ConsoleServices services;services.context=&c;services.assertion=assertion;services.log=log;ApplicationConsoleHooks hooks;hooks.context=&c;hooks.test_scope=scope;
 const bool result=args[0]==0?eeprom_console_self_test(services,hooks):max11613_console_self_test(services,hooks);
 if(c.events.size()>=8192)return 99;
 std::memcpy(events,c.events.c_str(),c.events.size()+1);out[0]=c.count;out[1]=c.clock;return result?0:1;
}
extern "C" unsigned hardware_console_missing_services(){ConsoleServices services;ApplicationConsoleHooks hooks;return (!eeprom_console_self_test(services,hooks)?1u:0u)|(!max11613_console_self_test(services,hooks)?2u:0u);}
