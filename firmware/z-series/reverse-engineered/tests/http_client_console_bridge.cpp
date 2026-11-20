#include "zseries/http_client_console_tests.hpp"
#include "zseries/diagnostics.hpp"
#include "oem_credentials.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace zseries::oem;
namespace {
enum Index {CLOCK,COUNTER,INITIAL_RESULT,INITIAL_CLASS,CLASS_MODE,STACK_FIRST,STACK_TEST,STACK_LAST,
    HEAP_BEFORE,HEAP_AFTER,FAIL_CONFIG_SCOPE,FAIL_CONNECT_SCOPE,WRONG_UNREACHABLE,SOCKET_ERRNO,
    READ_LIMIT,READ_ERROR_AT,YIELD_STEP,WRITE_SHORT,NO_REPLY_SCOPE,WORDS};
struct Fixture {
    const std::uint32_t* in;std::uint32_t now;DiagnosticState diagnostics;
    std::string response,wire;std::size_t position=0;unsigned acquires=0,scope=0,heaps=0,writes=0,waters=0,yields=0,watchdogs=0;
    std::vector<std::vector<std::uint32_t>> events;
    struct Frame{std::uint32_t date,count;};std::vector<Frame> tests,scopes;
    unsigned failure_kind=0,failure_at=0,failure_calls=0,resource_calls=0;
    Fixture(const std::uint32_t* words,const char* value):in(words),now(words[CLOCK]),response(value){diagnostics.assertion_count=words[COUNTER];}
    bool accepts(unsigned kind){return failure_kind!=kind||++failure_calls!=failure_at;}
};
void bytes(std::vector<std::uint32_t>& out,const void* data,std::size_t size){
    out.push_back(static_cast<std::uint32_t>(size));const auto* p=static_cast<const std::uint8_t*>(data);
    for(std::size_t i=0;i<size;++i)out.push_back(p[i]);
}
void event(Fixture& f,std::uint32_t kind,std::uint32_t a=0,std::uint32_t b=0,const std::string& text={},const void* data=nullptr,std::size_t size=0){
    std::vector<std::uint32_t> row{kind,a,b};bytes(row,text.data(),text.size());bytes(row,data,size);f.events.push_back(row);
}
std::uint32_t clock(void* p){return static_cast<Fixture*>(p)->now;}
std::string format(const char* pattern,std::uint32_t a,std::uint32_t b=0){char text[160];::snprintf(text,sizeof text,pattern,a,b);return text;}
void line(Fixture& f,std::uint32_t kind,const std::string& message){
    const double seconds=static_cast<float>(f.now)/1000.0f;
    event(f,kind,kind==16?f.diagnostics.assertion_count:0,0,message,&seconds,sizeof seconds);
}
void assertion_log(void* p,double seconds,const char* file,std::uint32_t source,const char* expression){
    auto& f=*static_cast<Fixture*>(p);
    const auto message=std::string("[E_][@][ASSERT][")+file+format(":%u] ",source)+expression;
    event(f,16,f.diagnostics.assertion_count,0,message,&seconds,sizeof seconds);
}
void delay(void* p,std::uint32_t value){auto& f=*static_cast<Fixture*>(p);event(f,17,value,f.diagnostics.assertion_count);f.now+=value;}
bool assertion(void* p,const char* file,std::uint32_t source,const char* expression){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(1))return false;
    DiagnosticServices services;services.context=p;services.millis=clock;services.assertion_log=assertion_log;services.delay_ms=delay;
    return record_assertion(f.diagnostics,services,file,source,expression)==DiagnosticResult::Ok;
}
bool log(void* p,ConsoleLevel level,const char* tag,const char* file,std::uint32_t source,const char* message){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(2))return false;
    const char* prefix=level==ConsoleLevel::Error?"E_":level==ConsoleLevel::Warning?"W_":level==ConsoleLevel::Debug?"D_":file?"I_":"IV";
    std::string value=std::string("[")+prefix+"][@]["+tag+"]";
    if(file)value+="["+std::string(file)+format(":%u]",source);
    line(f,18,value+" "+message);return true;
}
std::uint32_t water(void* p){
    auto& f=*static_cast<Fixture*>(p);const auto value=f.waters==0?f.in[STACK_FIRST]:f.waters==9?f.in[STACK_LAST]:f.in[STACK_TEST];
    ++f.waters;event(f,23,0,value);return value;
}
bool watchdog(void* p){auto& f=*static_cast<Fixture*>(p);if(!f.accepts(7))return false;event(f,24);++f.watchdogs;return true;}
bool test_scope(void* p,bool begin,const char* tag,const char* file,std::uint32_t source,const char* name){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(3))return false;
    std::string prefix=std::string(begin?"[T>][@][":"[T_][@][")+tag+"]["+file+format(":%u] [",source)+name+"] ";
    if(begin){++f.scope;f.tests.push_back({f.now,f.diagnostics.assertion_count});line(f,18,prefix+"BEGIN");return watchdog(p);}
    if(f.tests.empty())return false;
    const auto frame=f.tests.back();f.tests.pop_back();
    const auto stack=water(p);if(stack<=700&&!assertion(p,"Assert.h",0x48,"stackHighWaterMark > 700"))return false;
    const auto failed=f.diagnostics.assertion_count-frame.count;
    line(f,18,prefix+(failed?"FAIL ":"OK ")+format("%d %ums",failed,f.now-frame.date));
    return log(p,ConsoleLevel::Warning,"SYST","Assert.h",0x4f,format("StackHighWaterMark = %u",stack).c_str())&&
        log(p,ConsoleLevel::Info,"SYST",nullptr,0,format("StackHighWaterMark = %u",stack).c_str());
}
bool scope(void* p,bool begin,const char* tag,const char* file,std::uint32_t source,const char* name){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(4))return false;std::uint32_t elapsed=0;
    if(begin)f.scopes.push_back({f.now,0});else{if(f.scopes.empty())return false;elapsed=f.now-f.scopes.back().date;f.scopes.pop_back();}
    line(f,18,std::string(begin?"[D>][@][":"[D<][@][")+tag+"]["+file+format(":%u] ",source)+name+(begin?" BEGIN ":" END   ")+format("%u",elapsed));return true;
}
bool yielding(void* p){auto& f=*static_cast<Fixture*>(p);if(!f.accepts(5))return false;event(f,22);++f.yields;f.now+=f.in[YIELD_STEP];return f.yields<2000;}
bool sleeping(void* p,std::uint32_t value){auto& f=*static_cast<Fixture*>(p);if(!f.accepts(6))return false;delay(p,value);return true;}
const char* resource(void* p,std::uint32_t address){auto& f=*static_cast<Fixture*>(p);++f.resource_calls;if(!f.accepts(9))return nullptr;return original_credentials::http_client_test_text(nullptr,address);}
bool perform(void* p,const HttpAction& action,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);const auto* in=f.in;if(!f.accepts(8))return false;
    event(f,static_cast<unsigned>(action.effect),action.effect==HttpEffect::ReadBytes?static_cast<std::uint32_t>(action.size):action.argument,
        action.second,action.text?action.text:"",action.data,action.data?action.size:0);result=0;
    switch(action.effect){
    case HttpEffect::AcquireClient:
        if(f.acquires++==0){result=in[INITIAL_RESULT];*action.acquired_class=static_cast<HttpClientClass>(in[INITIAL_CLASS]);}
        else if(f.scope==in[FAIL_CONFIG_SCOPE]){result=0;*action.acquired_class=HttpClientClass::None;}
        else{result=1;*action.acquired_class=in[CLASS_MODE]==1?HttpClientClass::Ethernet:in[CLASS_MODE]==2?HttpClientClass::Wifi:in[CLASS_MODE]==3?HttpClientClass::WifiSecure:action.argument?HttpClientClass::WifiSecure:HttpClientClass::Wifi;}
        break;
    case HttpEffect::PrepareEthernet:break;
    case HttpEffect::FreeHeap:result=(f.heaps++&1)?in[HEAP_AFTER]:in[HEAP_BEFORE];break;
    case HttpEffect::ConnectHost:case HttpEffect::ConnectAddress:
        f.position=0;f.wire=f.scope==in[NO_REPLY_SCOPE]?"":f.response;
        result=f.scope==in[FAIL_CONNECT_SCOPE]||(f.scope>=7&&!in[WRONG_UNREACHABLE])?0:1;break;
    case HttpEffect::Write:result=static_cast<std::uint32_t>(action.size);if(++f.writes==in[WRITE_SHORT]&&result)--result;break;
    case HttpEffect::Available:result=static_cast<std::uint32_t>(std::min<std::size_t>(f.wire.size()-f.position,in[READ_LIMIT]));break;
    case HttpEffect::ReadByte:
        result=f.position==in[READ_ERROR_AT]||f.position==f.wire.size()?0xffffffffu:static_cast<unsigned char>(f.wire[f.position++]);break;
    case HttpEffect::ReadBytes:{const auto count=std::min(action.size,f.wire.size()-f.position);std::memcpy(action.destination,f.wire.data()+f.position,count);f.position+=count;result=static_cast<std::uint32_t>(count);break;}
    case HttpEffect::Connected:result=f.position<f.wire.size();break;
    case HttpEffect::SocketError:result=in[SOCKET_ERRNO];break;
    default:break;
    }return true;
}
}
extern "C" int http_client_console_case(const std::uint32_t* input,const char* wire,unsigned missing,unsigned failure_kind,unsigned failure_at,std::uint32_t* output,std::size_t capacity){
    Fixture f(input,wire);f.failure_kind=failure_kind;f.failure_at=failure_at;
    HttpServices http;http.context=&f;http.millis=clock;http.perform=perform;
    ConsoleServices diagnostics;diagnostics.context=&f;diagnostics.assertion=assertion;diagnostics.log=log;
    ApplicationConsoleHooks hooks;hooks.context=&f;hooks.millis=clock;hooks.stack_high_water=water;
    hooks.test_scope=test_scope;hooks.scope=scope;hooks.yield=yielding;hooks.delay_ms=sleeping;hooks.watchdog_reset=watchdog;
    HttpClientTestResources resources;resources.context=&f;resources.text=resource;
    switch(missing){case 1:http.perform=nullptr;break;case 2:http.millis=nullptr;break;case 3:diagnostics.assertion=nullptr;break;
    case 4:diagnostics.log=nullptr;break;case 5:hooks.millis=nullptr;break;case 6:hooks.stack_high_water=nullptr;break;
    case 7:hooks.test_scope=nullptr;break;case 8:hooks.scope=nullptr;break;case 9:hooks.yield=nullptr;break;
    case 10:hooks.delay_ms=nullptr;break;case 11:hooks.watchdog_reset=nullptr;break;case 12:resources.text=nullptr;break;}
    const auto result=http_client_console_test(http,diagnostics,hooks,resources);
    std::vector<std::uint32_t> all{static_cast<unsigned>(result),f.now,f.diagnostics.assertion_count,f.acquires,f.scope,f.heaps,
        static_cast<std::uint32_t>(f.position),f.writes,f.waters,f.yields,f.watchdogs,
        static_cast<std::uint32_t>(f.tests.size()),static_cast<std::uint32_t>(f.scopes.size()),static_cast<std::uint32_t>(f.events.size())};
    for(const auto& row:f.events){all.push_back(static_cast<std::uint32_t>(row.size()));all.insert(all.end(),row.begin(),row.end());}
    if(all.size()+1>capacity)return 1;
    output[0]=static_cast<std::uint32_t>(all.size());std::copy(all.begin(),all.end(),output+1);return 0;
}
