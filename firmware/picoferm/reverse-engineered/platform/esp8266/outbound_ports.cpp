#include "outbound_ports.hpp"
#include <Arduino.h>
#include <cstring>
namespace picoferm { namespace oem {
bool ArduinoOutboundClient::connect_ip(std::uint32_t address,unsigned port){return client_.connect(IPAddress(address),port);}
int ArduinoOutboundClient::available(){return client_.available();}
int ArduinoOutboundClient::read(){return client_.read();}
bool ArduinoOutboundClient::connected(){return client_.connected();}
void ArduinoOutboundClient::write(const char* bytes,std::size_t length){client_.write(reinterpret_cast<const std::uint8_t*>(bytes),length);}
void ArduinoOutboundClient::stop(){client_.stop();}
void ArduinoRuntime::delay_ms(std::uint32_t duration){::delay(duration);}
void ArduinoRuntime::decimal(float value,char* destination,std::size_t capacity){
    if(!destination||!capacity)return;
    char text[64];::dtostrf(static_cast<double>(value),4,2,text);auto length=std::strlen(text);if(length>=capacity)length=capacity-1;std::memcpy(destination,text,length);destination[length]=0;
}
}}
