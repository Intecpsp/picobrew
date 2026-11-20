#include "zseries/still_client.hpp"
#include "zseries/still_http.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
namespace {
struct Probe {std::uint32_t clock=1000,counter=0,operations=0;bool accept_assertion=true,accept_log=true;};
bool assertion(void* context,const char*,std::uint32_t,const char*){auto& p=*static_cast<Probe*>(context);if(!p.accept_assertion)return false;++p.counter;p.clock+=200;return true;}
bool log(void* context,ConsoleLevel,const char*,const char*,std::uint32_t,const char*){return static_cast<Probe*>(context)->accept_log;}
bool service(void* context,const PicoStillAction&,std::uint32_t& value){++static_cast<Probe*>(context)->operations;value=1;return true;}
std::uint32_t millis(void* context){return static_cast<Probe*>(context)->clock;}
bool http_service(void* context,const HttpAction&,std::uint32_t& value){++static_cast<Probe*>(context)->operations;value=1;return true;}
}
int main(){
    Probe p;ConsoleServices diagnostics;diagnostics.context=&p;diagnostics.assertion=assertion;diagnostics.log=log;
    EepromData settings;PicoStillController controller(settings);controller.state.id=PicoStillStateId::Scanning;
    controller.services.context=&p;controller.services.perform=service;controller.services.diagnostics=diagnostics;
    controller.services.diagnostics.assertion=nullptr;
    assert(controller.setup()==PicoStillResult::MissingService&&p.operations==1&&p.counter==0);
    controller.services.diagnostics=diagnostics;p.accept_assertion=false;
    assert(controller.setup()==PicoStillResult::ServiceFailure&&p.operations==2&&p.counter==0);
    p.accept_assertion=true;assert(controller.setup()==PicoStillResult::Ok&&p.counter==1&&p.clock==1200);
    controller.services.diagnostics.log=nullptr;
    assert(controller.transition(PicoStillStateId::Idle)==PicoStillResult::MissingService&&controller.state.id==PicoStillStateId::Scanning);
    PicoStillSamples samples;PicoStillState state;StillClient client(state,samples);client.services.diagnostics=diagnostics;
    assert(client.set_host("")==StillClientResult::Ok&&p.counter==2&&p.clock==1400);
    client.services.diagnostics.assertion=nullptr;
    assert(client.set_host("")==StillClientResult::MissingService);
    client.services.diagnostics=diagnostics;p.accept_log=false;
    assert(client.build_uri(Command::data)==StillClientResult::ServiceFailure);
    assert(!samples.parse(1000,"#bad#",5,&diagnostics)&&samples.diagnostics_result==ConsoleResult::ServiceFailure);
    p.accept_log=true;diagnostics.assertion=nullptr;
    const char* oversized="#1234567890123456,2,3,4,5,1,1,1,None#";
    assert(!samples.parse(1000,oversized,std::strlen(oversized),&diagnostics)&&samples.diagnostics_result==ConsoleResult::MissingService);
    diagnostics.assertion=assertion;client.services.diagnostics=diagnostics;
    StillHttpResponse response;response.status=200;assert(client.receive_headers(response));
    client.pending=Command::data;assert(client.receive_body("#bad#",5)==StillClientResult::Ok);
    const auto before=p.counter,now=p.clock;
    assert(client.complete(now)==StillClientResult::Ok&&p.counter==before+4&&p.clock==now+800);
    assert(client.succeeded()&&state.response_date==now&&samples.current.timestamp==0&&samples.t1.count==1);
    // A callback failure must survive the enclosing HTTP transition to Idle.
    HttpClient http;StillHttpBinding binding(http,client);http.services.context=&p;http.services.millis=millis;http.services.perform=http_service;
    client.services=still_http_services(binding);client.services.diagnostics=diagnostics;
    StillHttpAction configure(StillHttpEffect::Configure);configure.uri="http://192.168.4.1/data";std::uint32_t accepted=0;
    assert(client.services.perform(client.services.context,configure,accepted)&&accepted);
    http.state=HttpState::Complete;p.accept_log=false;
    assert(tick_still_http(binding,p.clock)==HttpResult::ServiceFailure&&http.state==HttpState::Idle);
    p.accept_log=true;assert(tick_still_http(binding,p.clock)==HttpResult::Ok);
}
