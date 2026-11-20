#include "zseries/http_client_console_tests.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem {
namespace {
struct Runner {
    const ConsoleServices& diagnostics;
    const ApplicationConsoleHooks& hooks;
    const HttpClientTestResources& resources;
    HttpClient client;
    ConsoleResult result=ConsoleResult::Ok;
    bool done=false,show_body=false;
    std::int32_t content_length=-1;
    const char* content_type=nullptr;
    std::uint32_t written=0;

    Runner(HttpServices http,const ConsoleServices& d,const ApplicationConsoleHooks& h,
            const HttpClientTestResources& r):diagnostics(d),hooks(h),resources(r){
        client.services=http;client.services.diagnostics=d;client.name="TEST";
    }
    bool fail(ConsoleResult value){if(result==ConsoleResult::Ok)result=value;return false;}
    bool http(HttpResult value){
        switch(value){case HttpResult::Ok:return true;
        case HttpResult::MissingService:return fail(ConsoleResult::MissingService);
        case HttpResult::ServiceFailure:return fail(ConsoleResult::ServiceFailure);
        case HttpResult::InvalidSpan:return fail(ConsoleResult::InvalidInput);}
        return fail(ConsoleResult::InvalidInput);
    }
    const char* text(std::uint32_t address){
        if(!resources.text){fail(ConsoleResult::MissingService);return nullptr;}
        const char* value=resources.text(resources.context,address);
        if(!value||!*value){fail(ConsoleResult::InvalidInput);return nullptr;}
        return value;
    }
    bool require(bool condition,std::uint32_t line,const char* expression){
        if(condition)return true;
        if(!diagnostics.assertion)return fail(ConsoleResult::MissingService);
        return diagnostics.assertion(diagnostics.context,"HttpClient.h",line,expression)||fail(ConsoleResult::ServiceFailure);
    }
    bool resource_require(bool condition,std::uint32_t line,std::uint32_t address){
        if(condition)return true;
        const char* expression=text(address);return expression&&require(false,line,expression);
    }
    bool log(ConsoleLevel level,const char* file,std::uint32_t line,const char* pattern,...){
        if(!diagnostics.log)return fail(ConsoleResult::MissingService);
        char message[1200];va_list args;va_start(args,pattern);
        const int n=::vsnprintf(message,sizeof message,pattern,args);va_end(args);
        if(n<0||static_cast<std::size_t>(n)>=sizeof message)return fail(ConsoleResult::InvalidInput);
        return diagnostics.log(diagnostics.context,level,"TEST",file,line,message)||fail(ConsoleResult::ServiceFailure);
    }
    bool scope(bool begin,std::uint32_t line,const char* name){
        if(!hooks.scope)return fail(ConsoleResult::MissingService);
        return hooks.scope(hooks.context,begin,"IMOK","HttpClient.h",line,name)||fail(ConsoleResult::ServiceFailure);
    }
    static bool headers(void* p,const HttpResponse& response){
        auto& self=*static_cast<Runner*>(p);
        if(!self.scope(true,0x454,"begin"))return false;
        self.done=false;self.content_length=response.content_length;
        self.content_type=response.content_type.data();self.written=0;
        return self.scope(false,0x454,"begin");
    }
    static void body(void* p,const std::uint8_t* bytes,std::size_t count){
        auto& self=*static_cast<Runner*>(p);
        if(self.result!=ConsoleResult::Ok||!self.scope(true,0x464,"write"))return;
        // HttpClient owns a 1025-byte buffer with its final byte terminated.
        // The original Stream deliberately prints the C string, not a count-
        // limited substring; preserve that observable diagnostic behavior.
        if(self.show_body&&!self.log(ConsoleLevel::Info,nullptr,0,"CONTENT = %s",reinterpret_cast<const char*>(bytes))){
            (void)self.scope(false,0x464,"write");return;
        }
        self.written+=static_cast<std::uint32_t>(count);
        if(!self.require(self.content_length>0,0x46a,"_contentLength > 0")||
           !self.require(self.written<=static_cast<std::uint32_t>(self.content_length),0x46b,"_written <= _contentLength")){
            (void)self.scope(false,0x464,"write");return;
        }
        (void)self.scope(false,0x464,"write");
    }
    static void complete(void* p){
        auto& self=*static_cast<Runner*>(p);if(self.result!=ConsoleResult::Ok)return;
        self.done=true;
        (void)self.require(self.written==static_cast<std::uint32_t>(self.content_length),0x45f,"_written == _contentLength");
    }
    HttpReceiver receiver(){HttpReceiver value;value.context=this;value.headers=headers;value.body=body;value.complete=complete;return value;}
    void reset_stream(bool show){done=false;content_length=-1;content_type=nullptr;written=0;show_body=show;}
    bool wait(){
        while(client.state!=HttpState::Idle){
            if(!hooks.yield(hooks.context))return fail(ConsoleResult::ServiceFailure);
            if(!http(client.tick(hooks.millis(hooks.context)))||result!=ConsoleResult::Ok)return false;
        }
        return require(client.error!=HttpClientErrorId::None||done,0x47f,"client.hasError() || stream.isDone()")&&http(client.report_timing());
    }
    bool pause(){
        return (hooks.delay_ms(hooks.context,100)||fail(ConsoleResult::ServiceFailure))&&
            (hooks.watchdog_reset(hooks.context)||fail(ConsoleResult::ServiceFailure));
    }
    ~Runner(){if(result!=ConsoleResult::Ok&&client.state!=HttpState::Idle)(void)client.cancel();}
};
struct TestScope {
    Runner& runner;std::uint32_t line;const char* name;bool active=false;
    TestScope(Runner& value,std::uint32_t source,const char* label):runner(value),line(source),name(label){}
    bool begin(){
        active=runner.hooks.test_scope(runner.hooks.context,true,"HTTPC","HttpClient.h",line,name);
        return active||runner.fail(ConsoleResult::ServiceFailure);
    }
    bool end(){
        if(!active)return false;
        active=false;
        return runner.hooks.test_scope(runner.hooks.context,false,"HTTPC","HttpClient.h",line,name)||runner.fail(ConsoleResult::ServiceFailure);
    }
    ~TestScope(){if(active)(void)end();}
};
struct Case {std::uint32_t line,uri,configure_line,configure_expression;const char* name;};
const Case Cases[]={
    {0x49c,0x3f411b78,0x49e,0x3f4130b9,"HttpClient_HTTP_GET"},
    {0x4a4,0x3f413124,0x4a8,0x3f41318e,"HttpClient_HTTPS_GET"},
    {0x4af,0x3f413124,0x4b3,0x3f41318e,"HttpClient_HTTPS_GET2"},
    {0x4ba,0x3f413260,0x4be,0x3f4132b2,"HttpClient_HTTPS_GET3"},
    {0x4c5,0x3f41332f,0x4c7,0x3f413368,"HttpClient_HTTPS_POST"},
    {0x4d4,0x3f413436,0x4d6,0x3f413446,"HttpClient_HTTP_GET_InvalidRequest"},
    {0x4dc,0x3f4134ce,0x4de,0x3f4134e9,"HttpClient_HTTP_GET_ConnectError"},
    {0x4e5,0x3f4135a0,0x4e7,0x3f4135bc,"HttpClient_HTTPS_GET_ConnectError"}
};
}
ConsoleResult http_client_console_test(HttpServices http,const ConsoleServices& diagnostics,
        const ApplicationConsoleHooks& hooks,const HttpClientTestResources& resources){
    if(!http.perform||!http.millis||!diagnostics.assertion||!diagnostics.log||!hooks.millis||
       !hooks.stack_high_water||!hooks.test_scope||!hooks.scope||!hooks.yield||!hooks.delay_ms||!hooks.watchdog_reset)
        return ConsoleResult::MissingService;
    Runner run(http,diagnostics,hooks,resources);
    HttpClientClass selected=HttpClientClass::None;HttpAction acquire(HttpEffect::AcquireClient);
    acquire.argument=1;acquire.acquired_class=&selected;std::uint32_t accepted=0;
    if(!http.perform(http.context,acquire,accepted))return ConsoleResult::ServiceFailure;
    if(!run.require(accepted!=0,0x48a,"res")||!run.require(selected!=HttpClientClass::None,0x48b,"c != nullptr"))return run.result;
    if(!accepted||selected==HttpClientClass::None){
        (void)run.log(ConsoleLevel::Error,"HttpClient.h",0x48d,"Cannot run test without network");return run.result;
    }
    // Fail explicitly if any required private resource is unavailable. The
    // public code never substitutes a synthetic endpoint or authorization.
    for(const auto& item:Cases)if(!run.text(item.uri)||!run.text(item.configure_expression))return run.result;
    for(const auto address:{0x3f41320du,0x3f413469u,0x3f41349bu,0x3f413516u,0x3f413517u,0x3f41351fu,0x3f41354fu})
        if(!run.text(address))return run.result;
    const char* authorization=run.text(0x3f412fd5),*post=run.text(0x3f4133b3),*post_expression=run.text(0x3f4133e0);
    if(!authorization||!post||!post_expression||std::strlen(post)!=44)return ConsoleResult::InvalidInput;
    const auto mark1=hooks.stack_high_water(hooks.context);
    if(!run.log(ConsoleLevel::Info,nullptr,0,"mark1 = %d",static_cast<std::int32_t>(mark1)))return run.result;
    for(unsigned i=0;i<8;++i){
        const auto& item=Cases[i];TestScope scope(run,item.line,item.name);if(!scope.begin())return run.result;
        run.reset_stream(i!=0);bool configured=false;
        const char* uri=run.text(item.uri);if(!uri)return run.result;
        if(!run.http(run.client.configure(uri,run.receiver(),HttpClientClass::None,configured)))return run.result;
        if(!run.resource_require(i==5?!configured:configured,item.configure_line,item.configure_expression))return run.result;
        if(i==5){
            if(!run.resource_require(run.client.error==HttpClientErrorId::InvalidRequest,0x4d7,0x3f413469)||
               !run.resource_require(run.client.socket_error==0,0x4d8,0x3f41349b))return run.result;
        }else{
            if(i>=1&&i<=4){
                const bool authorized=run.client.set_authorization(authorization);
                if(!run.http(run.client.request.diagnostic_result)||!run.resource_require(authorized,item.configure_line+1,0x3f41320d))return run.result;
            }
            bool sent=false;
            if(i==4){
                if(!run.http(run.client.post("application/json",post,44,sent))||!run.require(sent,0x4cf,post_expression))return run.result;
            }else{
                if(!run.http(run.client.get(sent)))return run.result;
                const auto line=i==0?0x49fu:(i<4?item.configure_line+2:item.configure_line+1);
                if(!run.resource_require(i>=6?!sent:sent,line,i>=6?0x3f413516:0x3f413517))return run.result;
            }
            if(i<5){if(!run.wait())return run.result;}
            else if(!run.resource_require(run.client.error==HttpClientErrorId::ConnectError,item.configure_line+2,0x3f41351f)||
                !run.resource_require((static_cast<std::uint32_t>(selected)-2>=2)||run.client.socket_error==113,item.configure_line+3,0x3f41354f))return run.result;
        }
        if(!scope.end())return run.result;
        if(i<7&&!run.pause())return run.result;
    }
    const auto mark2=hooks.stack_high_water(hooks.context);
    if(!run.log(ConsoleLevel::Info,nullptr,0,"mark2 = %d",static_cast<std::int32_t>(mark2))||
       !run.require(mark2<=mark1,0x4ef,"mark2 <= mark1")||
       !run.log(ConsoleLevel::Info,nullptr,0,"DELTA = %d",static_cast<std::int32_t>(mark1-mark2))||
       !run.require(run.client.state==HttpState::Idle,0x32c,"_state == State::Idle"))return run.result;
    return ConsoleResult::Ok;
}
}} // namespace zseries::oem
