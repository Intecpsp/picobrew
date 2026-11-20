#include "hardware_bridge.cpp"
#include "picoferm/wifi.hpp"
namespace {
struct FakeWifi final:WifiPorts {
    const std::uint32_t* words;
    explicit FakeWifi(const std::uint32_t* w):words(w){}
    void persistent(bool value) override{ports.events.push_back("persistent:"+std::to_string(value));}
    void auto_connect(bool value) override{ports.events.push_back("auto_connect:"+std::to_string(value));}
    void mode(unsigned value) override{ports.events.push_back("mode:"+std::to_string(value));}
    std::uint32_t status() override{ports.events.push_back("status:"+std::to_string(words[10]));return words[10];}
    void disconnect_station(bool value) override{ports.events.push_back("disconnect_station:"+std::to_string(value));}
    void disconnect_access_point(bool value) override{ports.events.push_back("disconnect_ap:"+std::to_string(value));}
    void scan_delete() override{ports.events.push_back("scan_delete");}
    void scan(bool asynchronous,bool hidden) override{ports.events.push_back("scan:"+std::to_string(asynchronous)+":"+std::to_string(hidden));}
    int scan_complete() override{ports.events.push_back("scan_complete:"+std::to_string(static_cast<std::int32_t>(words[11])));return words[11];}
    void ap_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t mask) override{ports.events.push_back("ap_config:"+std::to_string(local)+":"+std::to_string(gateway)+":"+std::to_string(mask));}
    bool ap_start(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients) override{ports.events.push_back(std::string("ap_start:")+ssid+":"+password+":"+std::to_string(channel)+":"+std::to_string(hidden)+":"+std::to_string(clients));return words[12];}
    std::uint32_t begin_station(const char* ssid,const char* password,unsigned channel,bool connect) override{ports.events.push_back(std::string("begin_station:")+ssid+":"+password+":"+std::to_string(channel)+":"+std::to_string(connect));return words[13];}
    int resolve(const char* hostname,std::uint32_t& address) override{ports.events.push_back(std::string("resolve:")+hostname);address=words[15];return words[14];}
    std::uint32_t local_ip() override{ports.events.push_back("local_ip:"+std::to_string(words[16]));return words[16];}
    std::uint32_t gateway_ip() override{ports.events.push_back("gateway_ip:"+std::to_string(words[17]));return words[17];}
};
struct FakeWifiEvents final:WifiEvents {void wifi_changed(WifiState previous,WifiState next) override{ports.events.push_back("wifi_event:"+std::to_string(static_cast<unsigned>(previous))+":"+std::to_string(static_cast<unsigned>(next)));}};
std::string wifi_error,wifi_label;
}
extern "C" {
void ferm_wifi_run(unsigned op,unsigned arg,std::uint32_t* w,const char* error,unsigned empty){
    ports.events.clear();FakeHardware hw;hw.now=w[9];FakeWifi network(w);FakeWifiEvents events;Wifi wifi(ports,hw,network,events);
    wifi.flush_timeout=w[0];wifi.state=static_cast<WifiState>(w[1]);wifi.target=static_cast<WifiState>(w[2]);wifi.changed_at=w[3];wifi.internet_ready=w[4];wifi.begin_failed=w[5];wifi.begin_status=w[6];wifi.ap_ip=w[7];wifi.host_ip=w[8];wifi.error=error;
    wifi.ap_ssid=empty?"":"PicoFerm_123456";wifi.ssid="HomeSSID";wifi.password="WifiPassword";wifi.website="www.picobrew.com";wifi_label.clear();
    switch(op){case 0:wifi.set_state(static_cast<WifiState>(arg));break;case 1:wifi.poll();break;case 2:wifi.fail(arg);break;
    case 3:wifi.request(static_cast<WifiState>(arg));break;case 4:wifi_label=wifi_state_name(static_cast<WifiState>(arg));break;
    case 5:wifi_label=wifi_status_name(arg);break;case 6:wifi.initialize();break;case 7:wifi.connect(empty?"":"NewSSID","NewPassword");break;}
    w[0]=wifi.flush_timeout;w[1]=static_cast<unsigned>(wifi.state);w[2]=static_cast<unsigned>(wifi.target);w[3]=wifi.changed_at;w[4]=wifi.internet_ready;w[5]=wifi.begin_failed;w[6]=wifi.begin_status;w[7]=wifi.ap_ip;w[8]=wifi.host_ip;wifi_error=wifi.error;
}
const char* ferm_wifi_error(){return wifi_error.c_str();}const char* ferm_wifi_label(){return wifi_label.c_str();}
}
