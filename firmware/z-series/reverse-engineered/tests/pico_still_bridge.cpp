#ifdef STILL_CONTINUATION_DIAGNOSTICS
#include "still_continuation_probe.hpp"
#define STILL_DIAGNOSTICS (&still_continuation_probe.services)
#else
#define STILL_DIAGNOSTICS nullptr
#endif
#include "zseries/pico_still.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
enum Index { STATE,PREVIOUS,NOW,POLLDATE,POLLDELAY,MAXRETRY,THRESHOLD,RETRY,PENDING,ERROR,RETRYABLE,DONE,
    AUTHPENDING,AUTHORIZED,CLEANED,COMMAND_PENDING,COMMAND,SELECTION,WIFI_PENDING,WIFI,ALLOW,SCAN_SCENARIO,
    SCAN_COUNT,IP_CODE,SAMPLE_DATE,SAMPLE_DELTA,RESPONSE_DATE,FAILURES,POLL_STOP,ASSERTION,ARGUMENT,NEXT,AUTH_START,WORDS };
const char* ips[]={"192.168.1.73","0.0.0.0",""};
const char* scans[]={"home-network","PicoStill_A","other","PicoStill_12345678901234567890","PicoStill_","PicoStill_Z","PicoStill_1","PicoStill_2","PicoStill_3","PicoStill_4","PicoStill_5","PicoStill_6"};
template<std::size_t N> void copy(std::array<char,N>& out,const char* value){std::strncpy(out.data(),value,N-1);}
void append_text(std::vector<std::uint32_t>& out,const char* value){const auto n=value?std::strlen(value):0;out.push_back(static_cast<std::uint32_t>(n));for(std::size_t i=0;i<n;++i)out.push_back(static_cast<unsigned char>(value[i]));}
struct Fixture {const std::uint32_t* input;PicoStillController* controller;std::vector<std::vector<std::uint32_t>> events;};
bool perform(void* context,const PicoStillAction& action,std::uint32_t& result){
    Fixture& f=*static_cast<Fixture*>(context);auto& observed=f.controller->observed;
    std::vector<std::uint32_t> row{static_cast<std::uint32_t>(action.effect),action.argument};
    append_text(row,action.text);append_text(row,action.credentials?action.credentials->ssid.data():nullptr);
    append_text(row,action.credentials?action.credentials->password.data():nullptr);row.push_back(action.credentials&&action.credentials->internet_expected);
    f.events.push_back(row);
#ifdef STILL_CONTINUATION_DIAGNOSTICS
    still_continuation_probe.service(f.events.size()-1);
#endif
result=1;
    switch(action.effect){
    case PicoStillEffect::AcquireRuntime:result=f.input[ALLOW];break;
    case PicoStillEffect::ScanCount:result=f.input[SCAN_COUNT];break;
    case PicoStillEffect::ScanSsid:
        copy(*action.ssid,f.input[SCAN_SCENARIO]==0?"no-match":f.input[SCAN_SCENARIO]==1?"PicoStill_A":scans[action.argument%12]);break;
    case PicoStillEffect::ClientClear:observed.pending=Command::Empty;observed.client_error=observed.client_retryable=observed.client_done=false;break;
    case PicoStillEffect::ClientCommand:case PicoStillEffect::ClientConfigure:
        observed.pending=action.effect==PicoStillEffect::ClientConfigure?Command::configure:static_cast<Command>(action.argument);
        observed.client_error=observed.client_retryable=observed.client_done=false;break;
    case PicoStillEffect::AuthorizationRequest:observed.authorization_pending=f.input[AUTH_START]!=0;break;
    default:break;
    }return true;
}
}
extern "C" int pico_still_test(unsigned operation,const std::uint32_t* a,std::uint32_t* output,std::size_t capacity){
    EepromData settings;copy(settings.wifi_ssid,"configured-home");copy(settings.wifi_password,"configured-password");
    PicoStillController c(settings);Fixture f{a,&c,{}};c.services.context=&f;c.services.perform=perform;
#ifdef STILL_CONTINUATION_DIAGNOSTICS
    c.services.diagnostics=still_continuation_probe.services;
#endif

    c.state.id=static_cast<PicoStillStateId>(a[STATE]);c.state.previous=static_cast<PicoStillStateId>(a[PREVIOUS]);
    c.next_poll_date=a[POLLDATE];c.poll_delay_ms=a[POLLDELAY];c.maximum_retries=static_cast<std::int32_t>(a[MAXRETRY]);
    c.failure_threshold=static_cast<std::int32_t>(a[THRESHOLD]);c.retry_count=static_cast<std::int32_t>(a[RETRY]);
    c.observed.pending=static_cast<Command>(a[PENDING]);c.observed.client_error=a[ERROR]!=0;c.observed.client_retryable=a[RETRYABLE]!=0;
    c.observed.client_done=a[DONE]!=0;c.observed.authorization_pending=a[AUTHPENDING]!=0;c.observed.authorized=a[AUTHORIZED]!=0;c.observed.cleaned=a[CLEANED]!=0;
    c.commands.pending=a[COMMAND_PENDING]!=0;c.commands.value.id=static_cast<StillCommandId>(a[COMMAND]);c.commands.value.selection=static_cast<std::int32_t>(a[SELECTION]);
    c.wifi_events.pending=a[WIFI_PENDING]!=0;c.wifi_events.value.type=static_cast<WifiEventType>(a[WIFI]);
    c.state.sample_date=a[SAMPLE_DATE];c.state.sample_delta_ms=a[SAMPLE_DELTA];c.state.response_date=a[RESPONSE_DATE];
    c.state.consecutive_failures=static_cast<std::int32_t>(a[FAILURES]);c.state.polling_stopped=a[POLL_STOP]!=0;c.assertion_line=a[ASSERTION];
    copy(c.state.error,"fixture error");copy(c.state.selected,"fixtureA");copy(c.state.version,"fixture-version");copy(c.state.ip,ips[a[IP_CODE]]);
    copy(c.selected_access_point,"PicoStill_fixtureA");copy(c.home_credentials.ssid,"saved-home");copy(c.home_credentials.password,"saved-password");c.home_credentials.internet_expected=true;
    c.state.available_count=8;for(std::size_t i=0;i<8;++i){copy(c.state.available[i],"fixture0");c.state.available[i][7]=static_cast<char>('0'+i);}
    PicoStillResult result=PicoStillResult::Ok;
    switch(operation){
    case 0:result=c.advance(a[NOW]);break;
    case 1:result=c.transition(static_cast<PicoStillStateId>(a[ARGUMENT]));break;
    case 2:result=c.retry(static_cast<Command>(a[ARGUMENT]),static_cast<std::int32_t>(a[MAXRETRY]));break;
    case 3:result=c.wait_for(static_cast<PicoStillStateId>(a[NEXT]),static_cast<Command>(a[ARGUMENT]),static_cast<std::int32_t>(a[MAXRETRY]));break;
    case 4:result=c.collect_scan();break;
    case 5:result=c.setup();break;
    case 6:result=c.tick(a[NOW]);break;
    default:return 1;
    }
    const auto& s=c.state;const auto& o=c.observed;
    std::vector<std::uint32_t> state{static_cast<std::uint32_t>(s.id),static_cast<std::uint32_t>(s.previous),static_cast<std::uint32_t>(c.retry_count),c.next_poll_date,c.assertion_line,
        c.commands.pending,c.wifi_events.pending,static_cast<std::uint32_t>(o.pending),o.client_error,o.client_retryable,o.client_done,o.authorization_pending,o.authorized,o.cleaned,
        s.available_count,s.sample_date,s.sample_delta_ms,s.response_date,static_cast<std::uint32_t>(s.consecutive_failures),s.polling_stopped,c.home_credentials.internet_expected};
    const char* strings[]={s.error.data(),s.selected.data(),s.version.data(),s.ip.data(),c.selected_access_point.data(),c.home_credentials.ssid.data(),c.home_credentials.password.data()};
    for(const char* text:strings)append_text(state,text);
    for(std::size_t i=0;i<8;++i){append_text(state,s.available[i].data());state.push_back(s.available_truncated[i]);}
    std::vector<std::uint32_t> all{static_cast<std::uint32_t>(result),static_cast<std::uint32_t>(state.size()),static_cast<std::uint32_t>(f.events.size())};
    all.insert(all.end(),state.begin(),state.end());
    for(const auto& row:f.events){all.push_back(static_cast<std::uint32_t>(row.size()));all.insert(all.end(),row.begin(),row.end());}
    if(all.size()+1>capacity)return 2;
    output[0]=static_cast<std::uint32_t>(all.size());std::copy(all.begin(),all.end(),output+1);return 0;
}
