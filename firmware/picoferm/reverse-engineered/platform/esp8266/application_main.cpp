// Concrete composition of the recovered PicoFerm 0.2.7 application.
#include "arduino_ports.hpp"
#include "hardware_ports.hpp"
#include "wifi_ports.hpp"
#include "outbound_ports.hpp"
#include "system_ports.hpp"
#include "http_ports.hpp"
#include "picoferm/http_server.hpp"
#include <ESP8266WiFi.h>
namespace picoferm { namespace oem {
namespace {
class Device final:public HttpDevice {
public:explicit Device(Application& app):app_(app){}
    CalibrationStatus calibration() override{return app_.calibration();}
    void configure(char* result,std::size_t capacity,const char* ssid,const char* password) override{app_.configure(result,capacity,ssid,password);}
    void factory_reset() override{app_.factory_reset();}void calibrate() override{app_.calibrate();}
    int scan_count() override{return WiFi.scanComplete();}
    const char* scan_ssid(unsigned index) override{ssid_=WiFi.SSID(index);return ssid_.c_str();}
private:Application& app_;String ssid_;
};
class Web final:public WebServices {
public:explicit Web(ArduinoHttpListener& listener):listener_(listener){}HttpServer* server=nullptr;
    void initialize_dns(unsigned ttl,unsigned reply) override{listener_.set_dns_ttl(ttl);listener_.set_dns_reply_code(reply);}
    bool start_dns(unsigned port,const char* domain,std::uint32_t address) override{return listener_.start_dns(port,domain,address);}
    void stop_dns() override{listener_.stop_dns();}void start_http() override{server->start();}
    void set_enabled(bool value) override{server->enabled=value;}void poll() override{server->poll();}
private:ArduinoHttpListener& listener_;
};
struct OwnedApplication {
    ArduinoPorts ports;ArduinoHardwarePorts hardware;ArduinoRuntime runtime;ArduinoWifiPorts network;ArduinoOutboundClient outbound;ArduinoSystemPorts system;ArduinoHttpListener listener;Web web;Application app;Device device;HttpServer http;
    OwnedApplication():ports(EEPROM),hardware(Wire),system(EEPROM),web(listener),
        app(ports,hardware,runtime,runtime,network,outbound,system,web),device(app),
        http(listener,ports,hardware,runtime,runtime,app.data,app.measurements.data,device,1000){web.server=&http;}
};
OwnedApplication& owned(){static OwnedApplication instance;return instance;}
}
Application& application(){return owned().app;}
}}
void setup(){picoferm::oem::application().begin();}
void loop(){picoferm::oem::application().loop();}
