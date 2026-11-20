#include "picoferm/wifi.hpp"
#include <stdio.h>
namespace picoferm { namespace oem {
const char* wifi_state_name(WifiState value) noexcept {
    static const char* const names[]={"Booting","Idle","Scanning","AccessPoint","Connecting","Checking","Connected","FlushingAPClient"};
    const auto i=static_cast<unsigned>(value);return i<8?names[i]:"Unknown";
}
const char* wifi_status_name(std::uint32_t status) noexcept {
    static const char* const names[]={"WL_IDLE_STATUS","WL_NO_SSID_AVAIL","WL_SCAN_COMPLETED","WL_CONNECTED","WL_CONNECT_FAILED","WL_CONNECTION_LOST","WL_DISCONNECTED"};
    if(status<7)return names[status];
    return status==255?"WL_NO_SHIELD":"<WL_UNKNOWN>";
}
void Wifi::ip_text(const char* key,std::uint32_t value){char text[16];snprintf(text,sizeof(text),"%u.%u.%u.%u",value&255,(value>>8)&255,(value>>16)&255,value>>24);diagnostics_.text("WIFI",key,text);}
void Wifi::initialize(){
    if(!ap_ssid||!*ap_ssid)diagnostic_assertion(diagnostics_);
    network_.auto_connect(false);network_.persistent(false);network_.mode(0);
    diagnostics_.text("WIFI","AP_SSID",ap_ssid?ap_ssid:"");diagnostics_.text("WIFI","AP_PASSWORD","PicoFerm12!");ip_text("AP_IP",ap_ip);
}
void Wifi::request(WifiState next){
    if(next!=WifiState::Idle&&next!=WifiState::AccessPoint&&next!=WifiState::Connected)diagnostic_assertion(diagnostics_);
    diagnostics_.text("WIFI","_target",wifi_state_name(next));target=next;
}
void Wifi::connect(const char* name,const char* pass){
    if(!name||!*name)diagnostic_assertion(diagnostics_);
    // Missing owned strings are kept valid after the returning diagnostic.
    ssid=name?name:"";password=pass?pass:"";error="";request(WifiState::Connected);
}
void Wifi::set_state(WifiState next){
    if(next==state)return;
    switch(next){
    case WifiState::Idle:network_.disconnect_access_point(false);network_.disconnect_station(false);network_.mode(0);break;
    case WifiState::Scanning:network_.mode(1);network_.scan_delete();network_.scan(true,false);break;
    case WifiState::AccessPoint:
        network_.mode(2);network_.ap_config(ap_ip,ap_ip,0x00ffffff);
        if(!network_.ap_start(ap_ssid,"PicoFerm12!",1,false,4))diagnostic_assertion(diagnostics_);
        break;
    case WifiState::Connecting:
        network_.mode(1);diagnostics_.text("WIFI","SSID",ssid);diagnostics_.text("WIFI","PASSWORD",password);
        begin_status=network_.begin_station(ssid,password,0,true);begin_failed=begin_status!=0&&begin_status!=6&&begin_status!=3;
        diagnostics_.text("WIFI","_wifiBeginStatus",wifi_status_name(begin_status&255));diagnostics_.boolean("WIFI","_wifiBeginFailed",begin_failed);break;
    case WifiState::Checking:
        internet_ready=false;if(network_.resolve(website,host_ip)==1){diagnostics_.text("WIFI","WEBSITE",website);ip_text("WEBIP",host_ip);internet_ready=true;}break;
    case WifiState::Connected:ip_text("localIP",network_.local_ip());ip_text("gatewayIP",network_.gateway_ip());break;
    default:break;
    }
    diagnostics_.text("WIFI","STATE",wifi_state_name(next));events_.wifi_changed(state,next);state=next;changed_at=hardware_.millis();
}
void Wifi::fail(std::uint32_t status){
    const char* message="";switch(status){case 1:message="SSID not found";break;case 4:message="Could not connect";break;
    case 5:message="Connection lost";break;case 6:message="Disconnected while trying to connect";break;case 255:message="No Wifi shield";break;default:break;}
    if(!*error){diagnostics_.text("WIFI","ERROR",message);error=message;}
    if(target==WifiState::Connected)request(WifiState::Idle);
    set_state(WifiState::Idle);diagnostics_.yield();diagnostics_.yield();
}
void Wifi::poll(){
    const auto status=network_.status();const auto now=hardware_.millis();
    switch(state){
    case WifiState::Booting:set_state(WifiState::Idle);break;
    case WifiState::Idle:if(target==WifiState::AccessPoint)set_state(WifiState::Scanning);else if(target==WifiState::Connected)set_state(WifiState::Connecting);break;
    case WifiState::Scanning:if((static_cast<std::uint32_t>(network_.scan_complete())&0x80)==0)set_state(WifiState::AccessPoint);break;
    case WifiState::AccessPoint:if(target!=WifiState::AccessPoint)set_state(WifiState::FlushingAPClient);break;
    case WifiState::Connecting:
        if(begin_failed){fail(begin_status&255);break;}
        if(status==0)break;
        if(status==3){diagnostics_.text("WIFI","WL_STATUS",wifi_status_name(3));set_state(WifiState::Checking);}
        else if(status==6){if(target!=WifiState::Connected)set_state(WifiState::Idle);}
        else {diagnostics_.text("WIFI","WL_STATUS",wifi_status_name(status&255));fail(status&255);}break;
    case WifiState::Checking:if(internet_ready)set_state(WifiState::Connected);else fail(status&255);break;
    case WifiState::Connected:
        if(status==3&&target==WifiState::Connected)break;
        diagnostics_.text("WIFI","WL_STATUS",wifi_status_name(status&255));if(target==WifiState::Connected)request(WifiState::Idle);set_state(WifiState::Idle);break;
    case WifiState::FlushingAPClient:if(now>changed_at+flush_timeout){network_.disconnect_access_point(true);set_state(WifiState::Idle);}break;
    default:break;
    }
}
}}
