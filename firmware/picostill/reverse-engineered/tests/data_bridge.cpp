#include "application_bridge.cpp"
#include "picostill/device.hpp"
#include "picostill/reports.hpp"
#include <stdexcept>
namespace {
// HTTP credential binding does not use Wi-Fi SDK operations. Any accidental
// dependency here fails instead of silently supplying a successful network.
struct UnusedNetwork final:WifiPorts,WifiEvents {
    unsigned station_status() override{throw std::logic_error("network");}
    void mode(unsigned) override{throw std::logic_error("network");}
    void scan(bool,bool,bool,unsigned) override{throw std::logic_error("network");}
    int scan_complete() override{throw std::logic_error("network");}
    void enable_access_point(bool) override{throw std::logic_error("network");}
    void access_point_config(std::uint32_t,std::uint32_t,std::uint32_t) override{throw std::logic_error("network");}
    bool start_access_point(const char*,const char*,unsigned,bool,unsigned) override{throw std::logic_error("network");}
    int wait_status_bits(unsigned,unsigned) override{throw std::logic_error("network");}
    std::uint32_t access_point_ip() override{throw std::logic_error("network");}
    bool disconnect_access_point(bool) override{throw std::logic_error("network");}
    std::uint8_t begin_station(const char*,const char*,unsigned,bool) override{throw std::logic_error("network");}
    void hostname(const char*) override{throw std::logic_error("network");}
    std::uint32_t local_ip() override{throw std::logic_error("network");}
    std::uint32_t gateway_ip() override{throw std::logic_error("network");}
    int resolve(const char*,std::uint32_t&) override{throw std::logic_error("network");}
    bool disconnect_station(bool,bool) override{throw std::logic_error("network");}
    void integer(const char*,const char*,std::uint32_t) override{throw std::logic_error("network");}
    void delay_ms(std::uint32_t) override{throw std::logic_error("network");}
    void wifi_event(EventType,StillError) override{throw std::logic_error("network");}
} network;
struct Formats final:ReportPorts {
    void integer(const char*,const char*,std::uint32_t) override{throw std::logic_error("unused integer");}
    void floating(const char*,const char*,float) override{throw std::logic_error("unused float");}
    void format_decimal(double value,int width,unsigned precision,char* destination,std::size_t capacity) override{
        // Boundary model follows the pinned Arduino dtostrf digit algorithm.
        // The fixture executes the original formatter body and float classifier.
        bool negative=value<0;if(negative)value=-value;double divisor=2;for(unsigned i=0;i<precision;++i)divisor*=10;value+=1/divisor;
        double scale=1;int digits=1;while(value>=10*scale){scale*=10;++digits;}value/=scale;
        std::string out;int padding=width-digits-(precision?precision+1:0)-(negative?1:0);while(padding-->0)out+=' ';if(negative)out+='-';
        int left=digits+precision;while(left-->0){int digit=static_cast<int>(value);if(digit>9)digit=9;out+=static_cast<char>('0'|digit);if(left==static_cast<int>(precision)&&precision)out+='.';value=(value-digit)*10;}
        snprintf(destination,capacity,"%s",out.c_str());
    }
    void console(const char*) override{throw std::logic_error("unused console");}
} formats;
struct DeviceServices final:DevicePorts {
    void credential_variable(const char* scope,const char* key,const char* value,bool mask) override{ports.events.push_back(std::string("credential:")+scope+":"+key+":"+value+":"+std::to_string(mask));}
    void factory_reset() override{throw std::logic_error("reset");}
    int scan_count() override{throw std::logic_error("scan");}
    const char* scan_ssid(unsigned) override{throw std::logic_error("scan");}
} device_ports;
}
extern "C" {
unsigned data_run(unsigned op,std::uint8_t* data,std::uint8_t* stored,std::uint8_t* out,const char* text,const char* other,unsigned number,unsigned capacity,const std::uint8_t* raw){
    static_assert(sizeof(DeviceData)==208,"Owned arrays match original data bytes");
    ports.events.clear();ports.clock=1000;diagnostics.assertion_count=0;Hardware hw(ports,ports,diagnostics,ports);Controller ctrl(hw,ports,diagnostics,ports);Leds leds(ports,diagnostics);WifiStatus status;BackendStatus backend;
    Application app(ctrl,leds,ports,diagnostics,services,status,backend);Wifi wifi(status,network,ports,diagnostics,network);ApplicationDevice device(app,ctrl,hw,wifi,status,backend,device_ports,diagnostics,"");
    std::memcpy(&app.data,data,208);std::memcpy(&app.stored,stored,100);unsigned result=0;
    switch(op){
    case 0:app.data.initialize(text,other,diagnostics);break;
    case 1:result=static_cast<unsigned char>(hex_digit(number,diagnostics));break;
    case 2:bounded_copy(reinterpret_cast<char*>(out),capacity,text,diagnostics);break;
    case 3:encode_hex(reinterpret_cast<char*>(out),capacity,raw,number,diagnostics);break;
    case 4:app.data.set_uid(text,diagnostics);break;
    case 5:app.data.set_ssid(text,diagnostics);break;
    case 6:app.data.set_password(text,diagnostics);break;
    case 7:device.configure(text,other);result=app.credentials_changed;break;
    case 8:{std::memcpy(hw.sensors.values.data(),raw,20);hw.pump.enabled=read(raw,20);hw.fans.power.enabled=read(raw,24);hw.fans.second.enabled=read(raw,28);ctrl.state=static_cast<HWState>(read(raw,32));ctrl.error=static_cast<StillError>(read(raw,36));
        ApplicationReports reports(ports,formats,diagnostics);reports.bind(hw,ctrl);reports.controller_changed();std::memcpy(out,reports.csv.data(),128);break;}
    }
    std::memcpy(data,&app.data,208);std::memcpy(stored,&app.stored,100);return result;
}
}
