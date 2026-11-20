#include "zseries/http_client.hpp"
#include "zseries/diagnostics.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
namespace {
struct Fixture {
    std::uint32_t now=0,operations=0,logs=0,delays=0,available=1;
    DiagnosticState counter;
    bool failed_assertion=false,failed_log=false,failed_operation=false;
};
std::uint32_t clock(void* p){return static_cast<Fixture*>(p)->now;}
void assertion_log(void*,double,const char*,std::uint32_t,const char*){}
void delay(void* p,std::uint32_t value){auto& f=*static_cast<Fixture*>(p);++f.delays;f.now+=value;}
bool assertion(void* p,const char* file,std::uint32_t line,const char* expression){
    auto& f=*static_cast<Fixture*>(p);if(f.failed_assertion)return false;
    DiagnosticServices io;io.context=p;io.millis=clock;io.assertion_log=assertion_log;io.delay_ms=delay;
    return record_assertion(f.counter,io,file,line,expression)==DiagnosticResult::Ok;
}
bool log(void* p,ConsoleLevel,const char*,const char*,std::uint32_t,const char*){auto& f=*static_cast<Fixture*>(p);++f.logs;return !f.failed_log;}
bool operation(void* p,const HttpAction& action,std::uint32_t& result){
    auto& f=*static_cast<Fixture*>(p);++f.operations;if(f.failed_operation)return false;result=0;
    switch(action.effect){
    case HttpEffect::FreeHeap:result=100000;break;
    case HttpEffect::ConnectHost:case HttpEffect::ConnectAddress:case HttpEffect::Connected:result=1;break;
    case HttpEffect::Write:result=action.size;break;
    case HttpEffect::Available:result=f.available;break;
    default:break;
    }return true;
}
void bind(HttpClient& client,Fixture& fixture){
    client.services.context=&fixture;client.services.millis=clock;client.services.perform=operation;
    client.services.diagnostics.context=&fixture;client.services.diagnostics.assertion=assertion;client.services.diagnostics.log=log;
}
}
int main(){
    {
        HttpClient client;Fixture f;bind(client,f);client.state=HttpState::Initialized;
        std::strcpy(client.request.host.data(),"preserved");f.counter.assertion_count=0xffffffffu;f.now=0xfffffff0u;
        assert(client.reset()==HttpResult::Ok);assert(client.state==HttpState::Initialized);
        assert(!std::strcmp(client.request.host.data(),"preserved"));assert(f.counter.assertion_count==0&&f.now==184&&f.delays==1&&f.operations==0);
        client.services.diagnostics.assertion=nullptr;assert(client.reset()==HttpResult::MissingService&&f.delays==1);
        client.services.diagnostics.assertion=assertion;f.failed_assertion=true;
        assert(client.reset()==HttpResult::ServiceFailure&&f.delays==1&&f.operations==0);
    }
    {
        HttpClient client;Fixture f;bind(client,f);client.error=HttpClientErrorId::ReadError;f.failed_log=true;
        assert(client.set_error(HttpClientErrorId::PrematureClose)==HttpResult::ServiceFailure);
        assert(client.error==HttpClientErrorId::ReadError&&f.counter.assertion_count==1&&f.now==200);
        assert(f.logs==1&&f.operations==0);
    }
    {
        HttpClient client;Fixture f;bind(client,f);bool accepted=false;HttpReceiver receiver;
        assert(client.configure("http://example.test/path",receiver,HttpClientClass::Wifi,accepted)==HttpResult::Ok&&accepted);
        // The timestamp is captured before the real helper's delay. The send
        // duration and next deadline must not be rewritten using the later time.
        assert(client.get(accepted)==HttpResult::Ok&&accepted);
        assert(f.now==200&&f.counter.assertion_count==1&&client.timing.send_duration==0);
        assert(client.state_date==0&&client.deadline==5000);
        assert(client.tick(f.now)==HttpResult::Ok&&client.state==HttpState::Headers);
        assert(f.now==400&&f.counter.assertion_count==2);
        assert(client.timing.headers_date==200&&client.timing.reply_duration==200&&client.deadline==5200);
    }
    {
        HttpClient client;Fixture f;bind(client,f);bool accepted=false;HttpReceiver receiver;
        assert(client.configure("http://example.test/path",receiver,HttpClientClass::Wifi,accepted)==HttpResult::Ok&&accepted);
        f.failed_log=true;assert(client.get(accepted)==HttpResult::ServiceFailure&&!accepted&&f.operations==0);
        assert(client.state==HttpState::Initialized);
        f.failed_log=false;f.failed_operation=true;
        assert(client.get(accepted)==HttpResult::ServiceFailure&&!accepted&&f.operations==1);
    }
    {
        HttpClient client;Fixture f;bind(client,f);bool accepted=true;HttpReceiver receiver;f.failed_log=true;
        assert(client.configure("invalid://example",receiver,HttpClientClass::Wifi,accepted)==HttpResult::ServiceFailure&&!accepted);
        assert(client.state==HttpState::Idle&&client.error==HttpClientErrorId::None&&f.operations==0);
        client.services.diagnostics.log=nullptr;
        assert(client.configure("",receiver,HttpClientClass::Wifi,accepted)==HttpResult::MissingService&&!accepted);
    }
}
