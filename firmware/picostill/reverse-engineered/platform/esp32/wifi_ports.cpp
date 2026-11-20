#include "wifi_ports.hpp"

namespace picostill { namespace oem {
void ArduinoWifiPorts::initialize(){::WiFi.persistent(false);::WiFi.mode(WIFI_OFF);::WiFi.setAutoReconnect(false);}
unsigned ArduinoWifiPorts::station_status(){return ::WiFi.status();}
void ArduinoWifiPorts::mode(unsigned value){::WiFi.mode(static_cast<wifi_mode_t>(value));}
void ArduinoWifiPorts::scan(bool asynchronous,bool hidden,bool passive,unsigned maximum_ms){::WiFi.scanNetworks(asynchronous,hidden,passive,maximum_ms);}
int ArduinoWifiPorts::scan_complete(){return ::WiFi.scanComplete();}
void ArduinoWifiPorts::enable_access_point(bool enabled){::WiFi.enableAP(enabled);}
void ArduinoWifiPorts::access_point_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t subnet){::WiFi.softAPConfig(IPAddress(local),IPAddress(gateway),IPAddress(subnet));}
bool ArduinoWifiPorts::start_access_point(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients){return ::WiFi.softAP(ssid,password,channel,hidden,clients);}
int ArduinoWifiPorts::wait_status_bits(unsigned bits,unsigned timeout){return ::WiFi.waitStatusBits(bits,timeout);}
std::uint32_t ArduinoWifiPorts::access_point_ip(){return static_cast<std::uint32_t>(::WiFi.softAPIP());}
bool ArduinoWifiPorts::disconnect_access_point(bool power_off){return ::WiFi.softAPdisconnect(power_off);}
std::uint8_t ArduinoWifiPorts::begin_station(const char* ssid,const char* password,unsigned channel,bool connect){return ::WiFi.begin(ssid,password,channel,nullptr,connect);}
void ArduinoWifiPorts::hostname(const char* value){::WiFi.setHostname(value);}
std::uint32_t ArduinoWifiPorts::local_ip(){return static_cast<std::uint32_t>(::WiFi.localIP());}
std::uint32_t ArduinoWifiPorts::gateway_ip(){return static_cast<std::uint32_t>(::WiFi.gatewayIP());}
int ArduinoWifiPorts::resolve(const char* host,std::uint32_t& address){IPAddress ip;const int result=::WiFi.hostByName(host,ip);address=static_cast<std::uint32_t>(ip);return result;}
bool ArduinoWifiPorts::disconnect_station(bool power_off,bool erase_config){return ::WiFi.disconnect(power_off,erase_config);}
void ArduinoWifiPorts::integer(const char* scope,const char* name,std::uint32_t value){serial_.printf("VAR [%s] %s=%u\r\n",scope,name,value);}
void ArduinoWifiPorts::delay_ms(std::uint32_t duration){::delay(duration);}
}} // namespace picostill::oem
