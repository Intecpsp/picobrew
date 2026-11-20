#include "picostill/wifi.hpp"
#include <stdio.h>

namespace picostill { namespace oem {
const char* wifi_status_label(std::uint8_t status) noexcept {
    switch(status){case 0:return "WL_IDLE_STATUS";case 1:return "WL_NO_SSID_AVAIL";case 2:return "WL_SCAN_COMPLETED";
    case 3:return "WL_CONNECTED";case 4:return "WL_CONNECT_FAILED";case 5:return "WL_CONNECTION_LOST";
    case 6:return "WL_DISCONNECTED";case 255:return "WL_NO_SHIELD";default:return "<WL_UNKNOWN>";}
}
void Wifi::ip_variable(const char* name,std::uint32_t address) {
    char text[16];snprintf(text,sizeof(text),"%u.%u.%u.%u",unsigned(address&255),unsigned((address>>8)&255),unsigned((address>>16)&255),unsigned(address>>24));
    hardware_.variable("WIFI",name,text);
}
void Wifi::set_state(WifiState next) {
    diagnostics_.require(static_cast<unsigned>(next)<10,"Wifi.h",0x60,"isEnumValid(state)");
    if(status_.state==next)return;
    char transition[128];snprintf(transition,sizeof(transition),"%s -> %s",to_string(status_.state),to_string(next));hardware_.variable("WIFI","TRANSITION",transition);
    status_.state=next;changed_at=hardware_.millis();
    switch(next) {
    case WifiState::Idle:network_.mode(0);break;
    case WifiState::Scanning:network_.scan(false,false,false,300);break;
    case WifiState::AccessPoint:
        diagnostics_.check(ap_ssid && *ap_ssid,"_apSsid != NULL && strlen(_apSsid) > 0");
        hardware_.variable("WIFI","AP_SSID",ap_ssid);hardware_.variable("WIFI","AP_PASSWORD","12345678");ip_variable("AP_IP",ap_ip);
        network_.mode(2);network_.enable_access_point(true);network_.delay_ms(100);
        network_.access_point_config(ap_ip,ap_ip,0x00ffffff);
        diagnostics_.check(network_.start_access_point(ap_ssid,"12345678",1,false,4),"WiFi.softAP(_apSsid, AccessPointPassword)");
        network_.delay_ms(500);diagnostics_.check(network_.wait_status_bits(1,1000)!=0,"WiFi.waitStatusBits(AP_STARTED_BIT, 1000)");
        diagnostics_.check(ap_ip==network_.access_point_ip(),"AccessPointIP == WiFi.softAPIP()");events_.wifi_event(EventType::AccessPointReady,StillError::None);break;
    case WifiState::PoweringUp:network_.mode(1);break;
    case WifiState::Connecting:{
        hardware_.variable("WIFI","SSID",status_.requested_ssid);hardware_.variable("WIFI","PASSWORD",status_.requested_password);
        const auto result=network_.begin_station(status_.requested_ssid,status_.requested_password,0,true);network_.hostname(ap_ssid);
        hardware_.variable("WIFI","WL_STATUS",wifi_status_label(result));break;}
    case WifiState::Checking:
        station_ip=network_.local_ip();station_ip_obtained=true;diagnostics_.check(station_ip!=0,"LocalIP != INADDR_NONE");internet_ready=false;
        if(network_.resolve(website,status_.website_ip)==1){hardware_.variable("WIFI","WEBSITE",website);ip_variable("WEBIP",status_.website_ip);internet_ready=true;}break;
    case WifiState::Connected:
        ip_variable("localIP",network_.local_ip());ip_variable("gatewayIP",network_.gateway_ip());events_.wifi_event(EventType::Connected,StillError::None);break;
    default:if(static_cast<unsigned>(next)==10)diagnostics_.message("Invalid state = WifiState::_EnumCount");break;
    }
}
void Wifi::fail(StillError error) {
    status_.requested=WifiState::Idle;set_state(WifiState::Idle);events_.wifi_event(EventType::Error,error);
}
void Wifi::poll() {
    const auto connection=network_.station_status(),now=hardware_.millis();
    switch(status_.state) {
    case WifiState::Booting:set_state(WifiState::Idle);break;
    case WifiState::Idle:if(status_.requested!=WifiState::Idle)set_state(WifiState::PoweringUp);break;
    case WifiState::Scanning:{
        const int count=network_.scan_complete();if(count<0)return;network_.integer("WIFI","SSIDCOUNT",static_cast<std::uint32_t>(count));
        if(!count){hardware_.variable("WIFI","FUNC","scanNetworks retry");network_.scan(true,false,false,300);return;}
        if(status_.requested==WifiState::AccessPoint)set_state(WifiState::AccessPoint);
        else if(status_.requested==WifiState::Connected)set_state(WifiState::Connecting);
        else {status_.requested=WifiState::Idle;set_state(WifiState::Idle);}break;}
    case WifiState::AccessPoint:if(status_.requested!=WifiState::AccessPoint)set_state(WifiState::AccessPointDisconnecting);break;
    case WifiState::AccessPointDisconnecting:
        if(now<=changed_at+disconnect_delay)return;
        if(!network_.disconnect_access_point(false))diagnostics_.message("ERROR with WiFi.softAPdisconnect()");
        set_state(WifiState::Idle);break;
    case WifiState::PoweringUp:if(now>changed_at+power_delay)set_state(WifiState::Scanning);break;
    case WifiState::Connecting:
        if(!connection)return;
        if(connection==3){hardware_.variable("WIFI","WL_STATUS",wifi_status_label(3));set_state(WifiState::Checking);}
        else if(connection==6){if(now>changed_at+connection_timeout)fail(StillError::WifiConnectingTimeout);}
        else {hardware_.variable("WIFI","WL_STATUS",wifi_status_label(static_cast<std::uint8_t>(connection)));fail(connection==1?StillError::WifiSsidNotFound:StillError::WifiInvalidCredentials);}break;
    case WifiState::Checking:if(internet_ready)set_state(WifiState::Connected);else fail(StillError::WifiNoInternet);break;
    case WifiState::Connected:
        if(connection==3 && status_.requested==WifiState::Connected)return;
        hardware_.variable("WIFI","WL_STATUS",wifi_status_label(static_cast<std::uint8_t>(connection)));set_state(WifiState::Disconnecting);break;
    case WifiState::Disconnecting:
        if(now<=changed_at+disconnect_delay)return;
        if(!network_.disconnect_station(true,true))diagnostics_.message("ERROR with WiFi.disconnect(true, true)");
        set_state(WifiState::Idle);break;
    default:if(static_cast<unsigned>(status_.state)==10)diagnostics_.message("Invalid _state = WifiState::_EnumCount");break;
    }
}
}} // namespace picostill::oem
