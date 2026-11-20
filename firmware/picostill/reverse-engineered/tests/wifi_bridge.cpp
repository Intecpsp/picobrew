#include "controller_bridge.cpp"
#include "picostill/wifi.hpp"
namespace {
struct Network final:WifiPorts,WifiEvents {
    const std::uint32_t* values=nullptr;
    void delay_ms(std::uint32_t duration) override{ports.delay_ms(duration);}
    unsigned station_status() override{ports.events.push_back("status:"+std::to_string(values[0]));return values[0];}
    void mode(unsigned value) override{ports.events.push_back("mode:"+std::to_string(value));}
    void scan(bool asynchronous,bool hidden,bool passive,unsigned maximum_ms) override{ports.events.push_back("scan:"+std::to_string(asynchronous)+":"+std::to_string(hidden)+":"+std::to_string(passive)+":"+std::to_string(maximum_ms));}
    int scan_complete() override{const auto n=static_cast<std::int32_t>(values[1]);ports.events.push_back("scan_complete:"+std::to_string(n));return n;}
    void enable_access_point(bool value) override{ports.events.push_back("enable_ap:"+std::to_string(value));}
    void access_point_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t subnet) override{ports.events.push_back("ap_config:"+std::to_string(local)+":"+std::to_string(gateway)+":"+std::to_string(subnet));}
    bool start_access_point(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients) override{ports.events.push_back(std::string("start_ap:")+ssid+":"+password+":"+std::to_string(channel)+":"+std::to_string(hidden)+":"+std::to_string(clients));return values[3];}
    int wait_status_bits(unsigned bits,unsigned timeout) override{ports.events.push_back("wait:"+std::to_string(bits)+":"+std::to_string(timeout));return values[4];}
    std::uint32_t access_point_ip() override{ports.events.push_back("ap_ip:"+std::to_string(values[5]));return values[5];}
    bool disconnect_access_point(bool value) override{ports.events.push_back("disconnect_ap:"+std::to_string(value));return values[10];}
    std::uint8_t begin_station(const char* ssid,const char* password,unsigned channel,bool connect) override{ports.events.push_back(std::string("begin_station:")+ssid+":"+password+":"+std::to_string(channel)+":"+std::to_string(connect));return values[2];}
    void hostname(const char* value) override{ports.events.push_back(std::string("hostname:")+value);}
    std::uint32_t local_ip() override{ports.events.push_back("local_ip:"+std::to_string(values[6]));return values[6];}
    std::uint32_t gateway_ip() override{ports.events.push_back("gateway_ip:"+std::to_string(values[7]));return values[7];}
    int resolve(const char* host,std::uint32_t& address) override{ports.events.push_back(std::string("resolve:")+host);address=values[9];return values[8];}
    bool disconnect_station(bool off,bool erase) override{ports.events.push_back("disconnect_station:"+std::to_string(off)+":"+std::to_string(erase));return values[11];}
    void integer(const char* scope,const char* name,std::uint32_t value) override{ports.events.push_back(std::string("integer:")+scope+":"+name+":"+std::to_string(value));}
    void wifi_event(EventType type,StillError error) override{ports.events.push_back("wifi_event:"+std::to_string(static_cast<unsigned>(type))+":"+std::to_string(static_cast<unsigned>(error)));}
} network;
std::string label;
}
extern "C" {
void wifi_run(unsigned op,std::uint8_t* b,unsigned arg,unsigned now,int empty,const std::uint32_t* values){
    ports.events.clear();ports.clock=now;diagnostics.assertion_count=0;network.values=values;WifiStatus status;
    status.state=static_cast<WifiState>(read(b,32));status.requested=static_cast<WifiState>(read(b,40));status.website_ip=read(b,60);status.requested_ssid="HomeSSID";status.requested_password="WifiPassword";
    Wifi wifi(status,network,ports,diagnostics,network);wifi.power_delay=read(b,0);wifi.disconnect_delay=read(b,4);wifi.connection_timeout=read(b,8);wifi.changed_at=read(b,36);
    wifi.ap_ssid=empty?"":"PicoStill0123";wifi.website="www.picobrew.com";wifi.ap_ip=read(b,52);wifi.station_ip=read(b,68);wifi.internet_ready=read(b,20,1);wifi.station_ip_obtained=read(b,72,1);
    label.clear();if(op==0)wifi.set_state(static_cast<WifiState>(arg));else if(op==1)wifi.poll();else if(op==2)wifi.fail(static_cast<StillError>(arg));else label=wifi_status_label(arg);
    write(b,32,static_cast<unsigned>(status.state));write(b,40,static_cast<unsigned>(status.requested));write(b,60,status.website_ip);write(b,36,wifi.changed_at);write(b,68,wifi.station_ip);write(b,20,wifi.internet_ready,1);write(b,72,wifi.station_ip_obtained,1);
}
const char* wifi_label(){return label.c_str();}
}
