#include "zseries/azure_console_tests.hpp"
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
namespace {
ConsoleResult converted(BackendResult result){
    switch(result){case BackendResult::Ok:return ConsoleResult::Ok;
    case BackendResult::MissingService:return ConsoleResult::MissingService;
    case BackendResult::ServiceFailure:return ConsoleResult::ServiceFailure;
    case BackendResult::InvalidSpan:return ConsoleResult::InvalidInput;}
    return ConsoleResult::InvalidInput;
}
ConsoleResult require(const ConsoleServices& services,bool condition,std::uint32_t line,const char* expression){
    if(condition)return ConsoleResult::Ok;
    if(!services.assertion)return ConsoleResult::MissingService;
    return services.assertion(services.context,"Azure.h",line,expression)?ConsoleResult::Ok:ConsoleResult::ServiceFailure;
}
struct Scope {
    const ApplicationConsoleHooks& hooks;std::uint32_t line;const char* name;bool active=false;
    Scope(const ApplicationConsoleHooks& h,std::uint32_t source,const char* label):hooks(h),line(source),name(label){}
    bool begin(){active=hooks.test_scope(hooks.context,true,"AZ","Azure.h",line,name);return active;}
    bool end(){if(!active)return false;active=false;return hooks.test_scope(hooks.context,false,"AZ","Azure.h",line,name);}
    ~Scope(){if(active)(void)end();}
};
struct DiagnosticBinding {
    AzureService& azure;ConsoleServices previous;
    DiagnosticBinding(AzureService& value,const ConsoleServices& services):azure(value),previous(value.services.diagnostics){azure.services.diagnostics=services;}
    ~DiagnosticBinding(){azure.services.diagnostics=previous;}
};
ConsoleResult wait(AzureService& azure,bool disabling,const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    const auto start=hooks.millis(hooks.context);
    while(disabling?azure.state!=AzureState::Disabled:azure.state==AzureState::Connecting){
        const auto now=hooks.millis(hooks.context);
        // This is the original absolute comparison, including uint32 wrap.
        if(start+1000u<=now)return require(services,false,0x29b,"now < timeout");
        const auto result=converted(azure.tick(now));if(result!=ConsoleResult::Ok)return result;
        if(!hooks.watchdog_reset(hooks.context))return ConsoleResult::ServiceFailure;
    }
    return ConsoleResult::Ok;
}
}
ConsoleResult azure_password_tests(AzureService& azure,const ConsoleServices& services){
    if(!services.assertion||!services.log)return ConsoleResult::MissingService;
    auto result=require(services,azure.host&&std::strcmp(azure.host,"picobrewdev.azure-devices.net")==0,
        0x2cf,"CString::areEquals(_iothub_url, \"picobrewdev.azure-devices.net\")");
    if(result!=ConsoleResult::Ok)return result;
#include "azure_console_password_vectors.inc"
    BackendText<128> output;
    for(unsigned i=0;i<2;++i){
        int crypto_result=0;
        result=converted(azure.generate_signature(devices[i],"1570308197",output,crypto_result));
        if(result!=ConsoleResult::Ok)return result;
        result=require(services,crypto_result==0,i?0x2d7:0x2d2,"ret == 0");if(result!=ConsoleResult::Ok)return result;
        if(std::strcmp(output.text.data(),expected[i])){
            char expression[200];const int n=::snprintf(expression,sizeof expression,"CString::areEquals(output.c_ptr(), \"%s\")",expected[i]);
            if(n<0||static_cast<std::size_t>(n)>=sizeof expression)return ConsoleResult::InvalidInput;
            result=require(services,false,i?0x2d8:0x2d3,expression);if(result!=ConsoleResult::Ok)return result;
        }
        char message[200];const int n=::snprintf(message,sizeof message,"HMAC = [%s] (%d bytes)",output.text.data(),static_cast<int>(output.length));
        if(n<0||static_cast<std::size_t>(n)>=sizeof message)return ConsoleResult::InvalidInput;
        if(!services.log(services.context,ConsoleLevel::Info,"AZ","Azure.h",i?0x2d9:0x2d4,message))return ConsoleResult::ServiceFailure;
    }
    return ConsoleResult::Ok;
}
ConsoleResult azure_console_self_test(AzureService& live,const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    if(!services.assertion||!services.log||!hooks.test_scope||!hooks.millis||!hooks.watchdog_reset||
       !live.services.millis||!live.services.perform||!live.services.assertion||!live.services.hmac||!live.key)
        return ConsoleResult::MissingService;
    DiagnosticBinding bind(live,services);
    PendingEvent<AzureCommand> commands;AzureService local(commands);local.services=live.services;
    local.host="picobrewdev.azure-devices.net";local.key=live.key;local.key_size=live.key_size;
    {
        Scope scope(hooks,0x2ae,"Azure::_generateHmac()");if(!scope.begin())return ConsoleResult::ServiceFailure;
        auto result=azure_password_tests(local,services);if(result!=ConsoleResult::Ok)return result;
        if(!scope.end())return ConsoleResult::ServiceFailure;
    }
    {
        Scope scope(hooks,0x2b2,"Azure::disconnect()");if(!scope.begin())return ConsoleResult::ServiceFailure;
        live.disconnect();auto result=wait(live,true,services,hooks);if(result!=ConsoleResult::Ok)return result;
        result=require(services,live.state==AzureState::Disabled,0x2b5,"azure.getState() == AzureState::Disabled");if(result!=ConsoleResult::Ok)return result;
        if(!scope.end())return ConsoleResult::ServiceFailure;
    }
    {
        Scope scope(hooks,0x2b8,"Azure::connect()");if(!scope.begin())return ConsoleResult::ServiceFailure;
        auto result=converted(live.connect());if(result!=ConsoleResult::Ok)return result;
        result=wait(live,false,services,hooks);if(result!=ConsoleResult::Ok)return result;
        result=require(services,live.state==AzureState::Listening,0x2bb,"azure.getState() == AzureState::Listening");if(result!=ConsoleResult::Ok)return result;
        if(!scope.end())return ConsoleResult::ServiceFailure;
    }
    {
        Scope scope(hooks,0x2be,"Azure::setState(AzureState::SessionLoaded)");if(!scope.begin())return ConsoleResult::ServiceFailure;
        auto result=converted(live.transition(AzureState::SessionLoaded));if(result!=ConsoleResult::Ok)return result;
        result=require(services,live.deadline!=0,0x2c0,"azure._stateTimeout > 0");if(result!=ConsoleResult::Ok)return result;
        if(!scope.end())return ConsoleResult::ServiceFailure;
    }
    Scope scope(hooks,0x2c3,"Azure::sessionDiscard()");if(!scope.begin())return ConsoleResult::ServiceFailure;
    auto result=converted(live.session_discard());if(result!=ConsoleResult::Ok)return result;
    result=require(services,live.state==AzureState::Listening,0x2c5,"azure.getState() == AzureState::Listening");if(result!=ConsoleResult::Ok)return result;
    return scope.end()?ConsoleResult::Ok:ConsoleResult::ServiceFailure;
}
}} // namespace zseries::oem
