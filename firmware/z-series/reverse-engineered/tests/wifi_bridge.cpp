#include "zseries/wifi.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
enum Index { STATE,TARGET,DATE,NOW,CLOCK,RSSI,LEVEL,RESOLVED,ACTIVE,ATTEMPTS,DEADLINE,DURATION,INITIAL,MAXIMUM,
    POWER_DELAY,DISCONNECT_DELAY,CONNECT_TIMEOUT,STATUS,MODE,SCAN_COUNT,SDK_RSSI,LOCAL,GATEWAY,DNS,DNS_RESULT,
    SUCCESS,REQUEST_SSID,REQUEST_PASSWORD,REQUEST_INTERNET,CONNECTED_SSID,CONNECTED_PASSWORD,CONNECTED_INTERNET,
    ASSERTION,ARGUMENT,AP_NAME,PRESENT,OLD_LOCAL,OLD_GATEWAY,OLD_DNS,WORDS };
const char* ssids[] = {"", "fixture-network", "other-network", "12345678901234567890123456789012"};
const char* passwords[] = {"", "fixture-password", "other-password", "1234567890123456789012345678901234567890123456789012345678901234"};
const char* names[] = {"ZSeries", "", nullptr, "FixtureZ"};
template<std::size_t N> void copy(std::array<char,N>& to,const char* from){std::strncpy(to.data(),from,N-1);}
WifiCredentials credentials(std::uint32_t s,std::uint32_t p,std::uint32_t i){WifiCredentials c;copy(c.ssid,ssids[s]);copy(c.password,passwords[p]);c.internet_expected=i!=0;return c;}
std::uint32_t code(const char* text,const char*const* table,std::size_t count){
    for(std::size_t i=0;i<count;++i)if((!text&&!table[i])||(text&&table[i]&&std::strcmp(text,table[i])==0))return static_cast<std::uint32_t>(i);
    return 0xffffffffu;
}
struct Fixture {const std::uint32_t* input;std::vector<std::uint32_t> trace;};
std::uint32_t clock(void* context){return static_cast<Fixture*>(context)->input[CLOCK];}
bool perform(void* context,const WifiAction& action,std::uint32_t& result){
    Fixture& f=*static_cast<Fixture*>(context);const std::uint32_t* a=f.input;
    std::uint32_t t=0,p=0,c=0;
    if(action.text)t=action.effect==WifiEffect::ResolveHost?(std::strcmp(action.text,"www.picobrew.com")==0?1:0xffffffffu):code(action.text,names,4);
    else if(action.effect==WifiEffect::AccessPoint||action.effect==WifiEffect::Hostname)t=2;
    if(action.password)p=std::strcmp(action.password,"12345678")==0?1:0xffffffffu;
    if(action.credentials)c=100*code(action.credentials->ssid.data(),ssids,4)+10*code(action.credentials->password.data(),passwords,4)+action.credentials->internet_expected;
    const std::uint32_t row[]={static_cast<std::uint32_t>(action.effect),action.argument,action.second,action.third,action.fourth,t,p,c};
    f.trace.insert(f.trace.end(),row,row+8);result=a[SUCCESS];
    switch(action.effect){
    case WifiEffect::Status:result=a[STATUS];break;
    case WifiEffect::Mode:result=a[MODE];break;
    case WifiEffect::ScanComplete:result=a[SCAN_COUNT];break;
    case WifiEffect::Rssi:result=a[SDK_RSSI];break;
    case WifiEffect::LocalAddress:result=a[LOCAL];break;
    case WifiEffect::GatewayAddress:result=a[GATEWAY];break;
    case WifiEffect::ResolveHost:*action.address=a[DNS];result=a[DNS_RESULT];break;
    case WifiEffect::MacAddress:for(std::size_t i=0;i<6;++i)(*action.mac)[i]=static_cast<std::uint8_t>(i+10);break;
    default:break;
    }
    return true;
}
}
extern "C" int wifi_test(unsigned operation,const std::uint32_t* a,std::uint32_t* result,
                          std::uint32_t* events,std::size_t capacity){
    Fixture f{a,{}};WifiManager wifi;wifi.services.context=&f;wifi.services.millis=clock;wifi.services.perform=perform;
    wifi.state.id=static_cast<WiFiStateId>(a[STATE]);wifi.target=static_cast<WiFiStateId>(a[TARGET]);wifi.state_date=a[DATE];
    wifi.state.rssi=static_cast<std::int32_t>(a[RSSI]);wifi.state.level=a[LEVEL];wifi.test_host_resolved=a[RESOLVED]!=0;
    wifi.backoff.active=a[ACTIVE]!=0;wifi.backoff.attempts=a[ATTEMPTS];wifi.backoff.deadline=a[DEADLINE];wifi.backoff.duration=a[DURATION];
    wifi.backoff.initial_duration=a[INITIAL];wifi.backoff.maximum_duration=a[MAXIMUM];
    wifi.powerup_delay_ms=a[POWER_DELAY];wifi.disconnect_delay_ms=a[DISCONNECT_DELAY];wifi.connecting_timeout_ms=a[CONNECT_TIMEOUT];
    wifi.state.requested=credentials(a[REQUEST_SSID],a[REQUEST_PASSWORD],a[REQUEST_INTERNET]);
    wifi.state.connected=credentials(a[CONNECTED_SSID],a[CONNECTED_PASSWORD],a[CONNECTED_INTERNET]);
    wifi.assertion_line=a[ASSERTION];wifi.access_point_name=names[a[AP_NAME]];
    wifi.state.local_address_present=a[PRESENT]!=0;wifi.state.local_address=a[OLD_LOCAL];wifi.state.gateway_address=a[OLD_GATEWAY];wifi.state.test_host_address=a[OLD_DNS];
    WifiResult status=WifiResult::Ok;
    switch(operation){
    case 0:status=wifi.tick(a[NOW]);break;
    case 1:status=wifi.transition(static_cast<WiFiStateId>(a[ARGUMENT]));break;
    case 2:status=wifi.fail(a[NOW],static_cast<WifiError>(a[ARGUMENT]));break;
    case 3:wifi.select_target(static_cast<WiFiStateId>(a[ARGUMENT]));break;
    case 4:wifi.connect(credentials(a[REQUEST_SSID],a[REQUEST_PASSWORD],a[REQUEST_INTERNET]));break;
    case 5:status=wifi.setup();break;
    case 6:wifi.backoff.start(a[NOW]);break;
    case 7:wifi.backoff.advance(a[NOW]);break;
    default:return 1;
    }
    if(f.trace.size()>capacity)return 2;
    std::copy(f.trace.begin(),f.trace.end(),events);
    const auto& s=wifi.state;const auto& b=wifi.backoff;
    const std::uint32_t values[]={static_cast<std::uint32_t>(status),static_cast<std::uint32_t>(f.trace.size()),
        static_cast<std::uint32_t>(s.id),static_cast<std::uint32_t>(wifi.target),wifi.state_date,static_cast<std::uint32_t>(s.rssi),s.level,
        wifi.test_host_resolved,b.active,b.attempts,b.deadline,b.duration,b.initial_duration,b.maximum_duration,wifi.assertion_line,
        code(s.requested.ssid.data(),ssids,4),code(s.requested.password.data(),passwords,4),s.requested.internet_expected,
        code(s.connected.ssid.data(),ssids,4),code(s.connected.password.data(),passwords,4),s.connected.internet_expected,
        s.local_address_present,s.local_address,s.gateway_address,s.test_host_address};
    std::copy(values,values+25,result);for(std::size_t i=0;i<6;++i)result[25+i]=wifi.mac_address[i];
    return 0;
}
