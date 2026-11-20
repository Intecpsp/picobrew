#include "zseries/azure_console_tests.hpp"
#include "zseries/diagnostics.hpp"
#include "oem_credentials.hpp"
#include <openssl/hmac.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace zseries::oem;
namespace {
enum Index {OP,CLOCK,COUNTER,STATE,ENABLED,CONNECTED,ACQUIRE,CONNECT_RESULT,IGNORE_DISCONNECT,STACK,WATCHDOG_STEP,
    HMAC_FAIL_CALL,HMAC_ERROR,HMAC_CORRUPT,HOST_KIND,KEY_MUTATE,INITIAL_DURATION,MAX_DURATION,CHANGED,DEADLINE,WORDS};
struct Fixture {
    const std::uint32_t* input;std::uint32_t now;DiagnosticState diagnostics;bool connected;
    unsigned connections=0,hmacs=0,watchdogs=0,scope_count=0;int hmac_error=0;
    std::vector<std::vector<std::uint32_t>> events;
    struct Frame{std::uint32_t now,counter;};std::vector<Frame> scopes;
    unsigned failure_kind=0,failure_at=0,failure_calls=0;
    explicit Fixture(const std::uint32_t* in):input(in),now(in[CLOCK]),connected(in[CONNECTED]!=0){diagnostics.assertion_count=in[COUNTER];}
    bool accepts(unsigned kind){return failure_kind!=kind||++failure_calls!=failure_at;}
};
void bytes(std::vector<std::uint32_t>& out,const void* data,std::size_t size){out.push_back(static_cast<std::uint32_t>(size));const auto* p=static_cast<const std::uint8_t*>(data);for(std::size_t i=0;i<size;++i)out.push_back(p[i]);}
void event(Fixture& f,std::uint32_t kind,std::uint32_t a=0,std::uint32_t b=0,const std::string& text={},const void* data=nullptr,std::size_t size=0){
    std::vector<std::uint32_t> row{kind,a,b};bytes(row,text.data(),text.size());bytes(row,data,size);f.events.push_back(row);
}
std::uint32_t clock(void* p){return static_cast<Fixture*>(p)->now;}
std::string format(const char* pattern,std::uint32_t a,std::uint32_t b=0){char text[160];::snprintf(text,sizeof text,pattern,a,b);return text;}
void line(Fixture& f,std::uint32_t kind,const std::string& message){const double seconds=static_cast<float>(f.now)/1000.0f;event(f,kind,kind==16?f.diagnostics.assertion_count:0,0,message,&seconds,sizeof seconds);}
void assertion_log(void* p,double seconds,const char* file,std::uint32_t source,const char* expression){
    auto& f=*static_cast<Fixture*>(p);event(f,16,f.diagnostics.assertion_count,0,std::string("[E_][@][ASSERT][")+file+format(":%u] ",source)+expression,&seconds,sizeof seconds);
}
void delay(void* p,std::uint32_t value){auto& f=*static_cast<Fixture*>(p);event(f,17,value,f.diagnostics.assertion_count);f.now+=value;}
bool assertion(void* p,const char* file,std::uint32_t source,const char* expression){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(1))return false;
    DiagnosticServices d;d.context=p;d.millis=clock;d.assertion_log=assertion_log;d.delay_ms=delay;
    return record_assertion(f.diagnostics,d,file,source,expression)==DiagnosticResult::Ok;
}
bool log(void* p,ConsoleLevel level,const char* tag,const char* file,std::uint32_t source,const char* message){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(2))return false;
    const char* prefix=level==ConsoleLevel::Error?"E_":level==ConsoleLevel::Warning?"W_":level==ConsoleLevel::Debug?"D_":file?"I_":"IV";
    std::string value=std::string("[")+prefix+"][@]["+tag+"]";if(file)value+="["+std::string(file)+format(":%u]",source);
    line(f,18,value+" "+message);return true;
}
bool azure_assertion(void* p,std::uint32_t source){
    const char* expression=nullptr;
    switch(source){case 0x15c:expression="_connectCalled";break;case 0x171:expression="!CString::isNullOrEmpty(deviceId)";break;
    case 0x172:expression="!CString::isNullOrEmpty(ttl)";break;case 0x17a:expression="!uri.isTruncated()";break;
    case 0x237:expression="_sas_key_len > 0";break;default:return false;}
    return assertion(p,"Azure.h",source,expression);
}
bool diagnostic(void* p,std::uint32_t source){
    auto& f=*static_cast<Fixture*>(p);const char* text=nullptr;const char* tag="AZ";ConsoleLevel level=ConsoleLevel::Info;char value[100];
    switch(source){case 0x161:level=ConsoleLevel::Error;text="SYS CLIENT UNAVAILABLE";break;case 0x165:text="SYS CLIENT AVAILABLE";break;
    case 0xdc:text="ATTEMPTING IOT CONNECTION";break;case 0xdf:text="IOT CONNECTION MADE";break;case 0xe2:level=ConsoleLevel::Error;text="IOT CONNECTION FAILED";break;
    case 0x191:case 0x1b2:level=ConsoleLevel::Error;tag="SHA";::snprintf(value,sizeof value,source==0x191?"mbedtls_md_hmac() error %d":"_generateHmac() error %d",f.hmac_error);text=value;break;
    case 0x209:text="Bad AzureState";break;case 0x289:text="Session loaded window timed out";break;default:return false;}
    return log(p,level,tag,"Azure.h",source,text);
}
std::uint32_t water(void* p){auto& f=*static_cast<Fixture*>(p);event(f,23,0,f.input[STACK]);return f.input[STACK];}
bool watchdog(void* p){auto& f=*static_cast<Fixture*>(p);if(!f.accepts(4))return false;event(f,24);++f.watchdogs;f.now+=f.input[WATCHDOG_STEP];return f.watchdogs<2000;}
bool scope(void* p,bool begin,const char* tag,const char* file,std::uint32_t source,const char* name){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(3))return false;
    std::string prefix=std::string(begin?"[T>][@][":"[T_][@][")+tag+"]["+file+format(":%u] [",source)+name+"] ";
    if(begin){++f.scope_count;f.scopes.push_back({f.now,f.diagnostics.assertion_count});line(f,18,prefix+"BEGIN");return watchdog(p);}
    if(f.scopes.empty())return false;
    const auto frame=f.scopes.back();f.scopes.pop_back();const auto stack=water(p);
    if(stack<=700&&!assertion(p,"Assert.h",0x48,"stackHighWaterMark > 700"))return false;
    const auto failed=f.diagnostics.assertion_count-frame.counter;line(f,18,prefix+(failed?"FAIL ":"OK ")+format("%d %ums",failed,f.now-frame.now));
    return log(p,ConsoleLevel::Warning,"SYST","Assert.h",0x4f,format("StackHighWaterMark = %u",stack).c_str())&&
        log(p,ConsoleLevel::Info,"SYST",nullptr,0,format("StackHighWaterMark = %u",stack).c_str());
}
int hmac(void* p,const std::uint8_t* key,std::size_t key_size,const std::uint8_t* input,std::size_t size,std::uint8_t output[32]){
    auto& f=*static_cast<Fixture*>(p);++f.hmacs;
    const bool failed=f.input[HMAC_FAIL_CALL]==f.hmacs||f.input[HMAC_FAIL_CALL]==0xffffffffu;
    f.hmac_error=failed?static_cast<std::int32_t>(f.input[HMAC_ERROR]):0;
    event(f,40,f.hmacs,static_cast<std::uint32_t>(f.hmac_error),std::string(reinterpret_cast<const char*>(key),key_size),input,size);
    if(failed)return f.hmac_error;
    unsigned bytes=0;if(!HMAC(EVP_sha256(),key,static_cast<int>(key_size),input,size,output,&bytes)||bytes!=32)return -1;
    if(f.input[HMAC_CORRUPT])output[0]^=1;
    return 0;
}
bool perform(void* p,const AzureAction& action,std::uint32_t& value){
    auto& f=*static_cast<Fixture*>(p);if(!f.accepts(5))return false;value=1;
    if(action.effect==AzureEffect::Diagnostic)return diagnostic(p,action.argument);
    std::string text=action.text?action.text:"";if(action.second)text+=std::string("|")+action.second;if(action.third)text+=std::string("|")+action.third;
    event(f,50+static_cast<unsigned>(action.effect),action.argument,0,text);
    switch(action.effect){case AzureEffect::Connected:value=f.connected;break;
    case AzureEffect::Disconnect:if(!f.input[IGNORE_DISCONNECT])f.connected=false;break;
    case AzureEffect::AcquireClient:value=f.input[ACQUIRE];break;
    case AzureEffect::Connect:++f.connections;value=f.input[CONNECT_RESULT];if(value)f.connected=true;break;
    default:break;}
    return true;
}
}
namespace zseries { namespace oem {
// MQTT delivery is outside these diagnostics. Fail explicitly if a fixture
// accidentally invokes the unrelated message parser instead of masking it.
BackendResult AzureService::receive(const char*,char*,std::size_t){return BackendResult::MissingService;}
}}
extern "C" int azure_console_case(const std::uint32_t* input,unsigned missing,unsigned failure_kind,unsigned failure_at,std::uint32_t* output,std::size_t capacity){
    Fixture f(input);f.failure_kind=failure_kind;f.failure_at=failure_at;
    PendingEvent<AzureCommand> queue;AzureService azure(queue);azure.services.context=&f;azure.services.millis=clock;
    azure.services.perform=perform;azure.services.assertion=azure_assertion;azure.services.hmac=hmac;
    const char* hosts[]={"picobrewdev.azure-devices.net","picobrew.azure-devices.net","",nullptr,"fixture.azure.invalid"};azure.host=hosts[input[HOST_KIND]];
    azure.device_id="fixture-live";std::array<std::uint8_t,32> key;std::memcpy(key.data(),original_credentials::azure_key,key.size());if(input[KEY_MUTATE])key[0]^=1;azure.key=key.data();azure.key_size=key.size();
    azure.username.set("fixture-user");azure.password.set("fixture-password");azure.subscribe_topic.set("fixture-topic");azure.publish_topic.set("fixture-publish");
    azure.state=static_cast<AzureState>(input[STATE]);azure.enabled=input[ENABLED]!=0;azure.changed_ms=input[CHANGED];azure.deadline=input[DEADLINE];
    azure.backoff.initial_duration=input[INITIAL_DURATION];azure.backoff.maximum_duration=input[MAX_DURATION];
    ConsoleServices diagnostics;diagnostics.context=&f;diagnostics.assertion=assertion;diagnostics.log=log;
    ApplicationConsoleHooks hooks;hooks.context=&f;hooks.test_scope=scope;hooks.millis=clock;hooks.watchdog_reset=watchdog;
    switch(missing){case 1:diagnostics.assertion=nullptr;break;case 2:diagnostics.log=nullptr;break;case 3:hooks.test_scope=nullptr;break;
    case 4:hooks.millis=nullptr;break;case 5:hooks.watchdog_reset=nullptr;break;case 6:azure.services.millis=nullptr;break;
    case 7:azure.services.perform=nullptr;break;case 8:azure.services.assertion=nullptr;break;case 9:azure.services.hmac=nullptr;break;case 10:azure.key=nullptr;break;}
    const auto result=input[OP]?azure_password_tests(azure,diagnostics):azure_console_self_test(azure,diagnostics,hooks);
    std::vector<std::uint32_t> all{static_cast<unsigned>(result),static_cast<std::uint32_t>(azure.state),azure.enabled,azure.changed_ms,azure.deadline,
        azure.backoff.active,azure.backoff.attempts,azure.backoff.deadline,azure.backoff.duration,f.now,f.diagnostics.assertion_count,
        f.connected,f.connections,f.scope_count,f.hmacs,f.watchdogs,queue.pending,static_cast<std::uint32_t>(queue.value.id),
        static_cast<std::uint32_t>(f.scopes.size()),static_cast<std::uint32_t>(f.events.size())};
    for(const auto& row:f.events){all.push_back(static_cast<std::uint32_t>(row.size()));all.insert(all.end(),row.begin(),row.end());}
    if(all.size()+1>capacity)return 1;
    output[0]=static_cast<std::uint32_t>(all.size());std::copy(all.begin(),all.end(),output+1);return 0;
}
