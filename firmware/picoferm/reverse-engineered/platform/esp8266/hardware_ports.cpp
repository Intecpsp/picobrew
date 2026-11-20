#include "hardware_ports.hpp"
#include <Arduino.h>
namespace picoferm { namespace oem {
std::uint32_t ArduinoHardwarePorts::millis(){return ::millis();}
void ArduinoHardwarePorts::pin_mode(unsigned pin,unsigned mode){::pinMode(pin,mode);}
std::int32_t ArduinoHardwarePorts::analog_read(unsigned pin){return ::analogRead(pin);}
void ArduinoHardwarePorts::wire_begin(unsigned sda,unsigned scl){wire_.begin(sda,scl);}
void ArduinoHardwarePorts::wire_begin_transmission(unsigned address){wire_.beginTransmission(static_cast<std::uint8_t>(address));}
void ArduinoHardwarePorts::wire_write(std::uint8_t value){wire_.write(value);}
void ArduinoHardwarePorts::wire_end_transmission(){wire_.endTransmission();}
void ArduinoHardwarePorts::wire_request(unsigned address,unsigned count){wire_.requestFrom(static_cast<std::uint8_t>(address),static_cast<std::uint8_t>(count));}
std::uint32_t ArduinoHardwarePorts::wire_read(){return static_cast<std::uint32_t>(wire_.read());}
}}
