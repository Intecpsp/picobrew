#include "system_ports.hpp"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
extern "C" {
#include <user_interface.h>
}
namespace picoferm { namespace oem {
void ArduinoSystemPorts::serial_begin(unsigned baud,unsigned config,unsigned mode,unsigned tx){Serial.begin(baud,static_cast<SerialConfig>(config),static_cast<SerialMode>(mode),static_cast<std::uint8_t>(tx));}
void ArduinoSystemPorts::serial_line(const char* text){Serial.println(text);}
void ArduinoSystemPorts::mac_address(std::uint8_t* bytes){WiFi.macAddress(bytes);}
void ArduinoSystemPorts::storage_begin(std::size_t size){storage_.begin(size);}
std::size_t ArduinoSystemPorts::storage_size(){return storage_.length();}
void ArduinoSystemPorts::storage_read(std::uint8_t* bytes,std::size_t size){for(std::size_t i=0;i<size;++i)bytes[i]=storage_.read(i);}
void ArduinoSystemPorts::remove_os_wifi_data(){::system_restore();}
void ArduinoSystemPorts::deep_sleep(std::uint64_t duration,unsigned mode){ESP.deepSleep(duration,static_cast<RFMode>(mode));}
void ArduinoSystemPorts::restart(){ESP.restart();}
void ArduinoSystemPorts::update_reboot(bool value){ESPhttpUpdate.rebootOnUpdate(value);}
int ArduinoSystemPorts::update(const char* url,const char* version){return ESPhttpUpdate.update(String(url),String(version));}
std::string ArduinoSystemPorts::update_error(){return std::string(ESPhttpUpdate.getLastErrorString().c_str());}
}}
