#include "zseries/basic_console_tests.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
using namespace zseries::oem;
namespace {
struct Context{std::uint32_t count,clock,water;std::vector<std::string>events;};
bool assertion(void*p,const char*file,std::uint32_t line,const char*expression){auto&c=*static_cast<Context*>(p);c.events.push_back("assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression);++c.count;c.clock+=200;return true;}
bool log(void*p,ConsoleLevel level,const char*tag,const char*file,std::uint32_t line,const char*message){static_cast<Context*>(p)->events.push_back("log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+(file?file:"")+"|"+std::to_string(line)+"|"+message);return true;}
bool test_scope(void*p,bool begin,const char*tag,const char*file,std::uint32_t line,const char*name){auto&c=*static_cast<Context*>(p);c.events.push_back("scope|"+std::to_string(begin?1:0)+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+name);if(!begin&&c.water<=700)assertion(p,"Assert.h",0x48,"stackHighWaterMark > 700");return true;}
bool trace_scope(void*p,bool begin,const char*tag,const char*file,std::uint32_t line,const char*name){return log(p,ConsoleLevel::Info,tag,file,line,(std::string(name)+(begin?" BEGIN 0":" END   0")).c_str());}
std::uint32_t millis(void*p){return static_cast<Context*>(p)->clock;}
std::uint32_t counter(void*p){return static_cast<Context*>(p)->count;}
std::uint32_t water(void*p){return static_cast<Context*>(p)->water;}
bool yield(void*){return true;}
bool message(void*p,const char*s){static_cast<Context*>(p)->events.push_back("message|"+std::string(s));return true;}
bool invoke(void*p,std::uint32_t callback,const char*s){if(callback!=0x400fe350)return false;static_cast<Context*>(p)->events.push_back("invoke|0x400fe350|"+std::string(s));return true;}
bool delay(void*p,std::uint32_t ms){auto&c=*static_cast<Context*>(p);c.clock+=ms;c.events.push_back("delay|"+std::to_string(ms));return true;}
bool write(void*p,const char*s,std::size_t n){static_cast<Context*>(p)->events.push_back("write|"+std::string(s,n));return true;}
std::string quoted(const std::string&s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32){char escape[7];::snprintf(escape,sizeof escape,"\\u%04x",c);out+=escape;}else out+=c;}return out+'"';}
}
extern "C" __attribute__((visibility("default"))) unsigned basic_console_test_native(const std::uint32_t*args,std::uint32_t*out,char*events,std::size_t capacity){
    Context context{args[2],args[1],args[3],{}};ConsoleServices services;services.context=&context;services.assertion=assertion;services.log=log;
    ApplicationConsoleHooks hooks;hooks.context=&context;hooks.test_scope=test_scope;hooks.scope=trace_scope;hooks.millis=millis;hooks.delay_ms=delay;hooks.write=write;
    hooks.assertion_count=counter;hooks.stack_high_water=water;hooks.yield=yield;hooks.test_message=message;hooks.invoke=invoke;
    const bool result=(args[0]==0x4011ed44||args[0]==0x4011fbac)?basic_console_self_test(services,hooks):basic_console_leaf(args[0],services,hooks);
    std::string output="[";for(std::size_t i=0;i<context.events.size();++i){if(i)output+=',';output+=quoted(context.events[i]);}output+=']';
    if(output.size()>=capacity)return 99;
    std::memcpy(events,output.c_str(),output.size()+1);out[0]=context.count;out[1]=context.clock;return result?0:1;
}

extern "C" __attribute__((visibility("default"))) unsigned basic_console_service_failures(){
    Context context{0,1000,4096,{}};ConsoleServices services;services.context=&context;services.assertion=assertion;services.log=log;
    ApplicationConsoleHooks hooks;hooks.context=&context;hooks.test_scope=test_scope;hooks.scope=trace_scope;hooks.millis=millis;hooks.delay_ms=delay;hooks.write=write;
    hooks.assertion_count=counter;hooks.stack_high_water=water;hooks.yield=yield;hooks.test_message=message;hooks.invoke=invoke;
    unsigned passed=0;
    for(unsigned i=0;i<3;++i){auto broken=services;auto h=hooks;
        if(i==0)broken.assertion=nullptr;else if(i==1)broken.log=nullptr;else h.test_scope=nullptr;
        if(!basic_console_leaf(0x4010e2b4,broken,h))passed|=1u<<i;
    }
    auto h=hooks;h.test_scope=[](void*,bool,const char*,const char*,std::uint32_t,const char*){return false;};
    if(!basic_console_leaf(0x4010e2b4,services,h))passed|=8;
    auto broken=services;broken.log=[](void*,ConsoleLevel,const char*,const char*,std::uint32_t,const char*){return false;};
    if(!basic_console_leaf(0x4010cb60,broken,hooks))passed|=16;
    if(!basic_console_leaf(0,services,hooks)&&!basic_console_leaf(0xffffffff,services,hooks))passed|=32;
    h=hooks;h.write=nullptr;bool missing=!basic_console_leaf(0x400d79ac,services,h);
    h=hooks;h.millis=nullptr;missing=missing&&!basic_console_leaf(0x4011272c,services,h);
    h=hooks;h.delay_ms=nullptr;missing=missing&&!basic_console_leaf(0x4011272c,services,h);
    if(missing)passed|=64;
    bool aggregate=true;
    for(unsigned i=0;i<4;++i){h=hooks;switch(i){case 0:h.millis=nullptr;break;case 1:h.assertion_count=nullptr;break;case 2:h.test_message=nullptr;break;default:h.write=nullptr;break;}
        aggregate=aggregate&&!basic_console_self_test(services,h);}
    h=hooks;h.invoke=[](void*,std::uint32_t,const char*){return false;};
    if(aggregate&&!basic_console_self_test(services,h))passed|=128;
    return passed;
}

#include "zseries/head_screens.hpp"
#include "../src/basic_console_paragraphs.inc"
extern "C" __attribute__((visibility("default"))) unsigned basic_console_paragraph_probe(){
    const unsigned widths[]={6,8,9,14,17},heights[]={8,16,11,16,20};
    for(unsigned font=0;font<5;++font)for(unsigned i=0;i<sizeof paragraph_samples/sizeof paragraph_samples[0];++i){
        const auto width=widths[font];HeadWrappedText result;if(!head_wrap_text(result,paragraph_samples[i],4,width))return width*1000+i*10+1;
        if(!head_wrap_text(result,paragraph_samples[i],68,width,60/(heights[font]+1)))return width*1000+i*10+2;
    }return 0;
}

#ifdef BASIC_CONSOLE_SANITIZER_MAIN
#include <memory>
int main(){
    const std::uint32_t callbacks[]={0x4010e2b4,0x400e18a4,0x4010e580,0x400e1e90,0x401015c0,0x400fa974,0x400e2c20,0x4010f1c4,0x40109dbc,0x4010cb60,0x400e2f4c,0x400f195c,0x400e3004,0x400e32c4,0x40117578,0x4011272c,0x400d79ac,0x400e38dc,0x400e3a54,0x400e3f10,0x400ed224,0x400ecad0,0x400e4544,0x40101844,0x400eb870,0x400e5158,0x4010e0e4,0x400ed2ec,0x400e1048,0x400e55a0,0x400f560c,0x400db514,0x400db56c,0x40111df8,0x4010f418,0x400e56d4,0x401011a4,0x4011ed44,0x4011fbac};
    std::unique_ptr<char[]> events(new char[1048576]);std::uint32_t out[2]={};
    for(auto callback:callbacks){const std::uint32_t args[]={callback,0xffffff80,0xffffffff,700};
        if(basic_console_test_native(args,out,events.get(),1048576))return 1;}
    return basic_console_service_failures()==255?0:2;
}
#endif
