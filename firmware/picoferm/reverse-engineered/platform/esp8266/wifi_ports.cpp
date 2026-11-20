#include "wifi_ports.hpp"
#include <ESP8266WiFi.h>
namespace picoferm { namespace oem {
void ArduinoWifiPorts::persistent(bool enabled){WiFi.persistent(enabled);}
void ArduinoWifiPorts::auto_connect(bool enabled){WiFi.setAutoConnect(enabled);}
void ArduinoWifiPorts::mode(unsigned mode){WiFi.mode(static_cast<WiFiMode_t>(mode));}
std::uint32_t ArduinoWifiPorts::status(){return WiFi.status();}
void ArduinoWifiPorts::disconnect_station(bool power_off){WiFi.disconnect(power_off);}
void ArduinoWifiPorts::disconnect_access_point(bool power_off){WiFi.softAPdisconnect(power_off);}
void ArduinoWifiPorts::scan_delete(){WiFi.scanDelete();}
void ArduinoWifiPorts::scan(bool asynchronous,bool hidden){WiFi.scanNetworks(asynchronous,hidden);}
int ArduinoWifiPorts::scan_complete(){return WiFi.scanComplete();}
void ArduinoWifiPorts::ap_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t mask){WiFi.softAPConfig(IPAddress(local),IPAddress(gateway),IPAddress(mask));}
bool ArduinoWifiPorts::ap_start(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients){return WiFi.softAP(ssid,password,channel,hidden,clients);}
std::uint32_t ArduinoWifiPorts::begin_station(const char* ssid,const char* password,unsigned channel,bool connect){return WiFi.begin(ssid,password,channel,nullptr,connect);}
int ArduinoWifiPorts::resolve(const char* hostname,std::uint32_t& address){IPAddress ip(address);int result=WiFi.hostByName(hostname,ip);address=static_cast<std::uint32_t>(ip);return result;}
std::uint32_t ArduinoWifiPorts::local_ip(){return static_cast<std::uint32_t>(WiFi.localIP());}
std::uint32_t ArduinoWifiPorts::gateway_ip(){return static_cast<std::uint32_t>(WiFi.gatewayIP());}
}}
