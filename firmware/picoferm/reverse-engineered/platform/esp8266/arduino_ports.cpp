#include "arduino_ports.hpp"
#include <Arduino.h>
#include <cstring>

namespace picoferm { namespace oem {
bool ArduinoPorts::begin_storage(std::size_t allocation) {
    eeprom_.begin(allocation);
    if (allocation < PersistentData::size) diagnostic_assertion(*this);
    return eeprom_.length() >= PersistentData::size;
}
void ArduinoPorts::load(PersistentData& data) {
    // Original 0x40203b30 checks allocation before copying; it still sanitizes
    // the existing record when the allocation is short.
    if (eeprom_.length() >= PersistentData::size) eeprom_.get(0, data.bytes);
    data.sanitize(*this);
}
void ArduinoPorts::initialize_button() { ::pinMode(13, INPUT); ::pinMode(15, OUTPUT); }
void ArduinoPorts::yield() { ::yield(); }
void ArduinoPorts::digital_write(unsigned pin,bool value) { ::digitalWrite(pin,value?HIGH:LOW); }
std::uint32_t ArduinoPorts::digital_read(unsigned pin) { return static_cast<std::uint32_t>(::digitalRead(pin)); }
void ArduinoPorts::persist(const std::uint8_t* bytes,std::size_t size) {
    // Same dirty-buffer comparison/copy and unconditional commit as 0x40202de0.
    if (size == PersistentData::size && eeprom_.length() >= PersistentData::size) {
        std::array<std::uint8_t,PersistentData::size> record;
        std::memcpy(record.data(),bytes,record.size());
        eeprom_.put(0,record);
    }
    commit_succeeded_=eeprom_.commit();
}
void ArduinoPorts::text(const char*,const char*,const char*) {
    // In 0.2.7, the unmasked 0x40202f50 body formats a timestamp and calls
    // Arduino yield seven times. No UART write survives in this image.
    // Masked-secret logging is outside the recovered call sites/API.
    char timestamp[48];
    const float seconds=static_cast<float>(::millis()) / 1000.0f;
    ::dtostrf(static_cast<double>(seconds),4,2,timestamp);
    for (unsigned i=0;i<7;++i) ::yield();
}
void ArduinoPorts::integer(const char* scope,const char* key,std::uint32_t value) {
    char buffer[28];std::int32_t signed_value;std::memcpy(&signed_value,&value,4);
    ::itoa(signed_value,buffer,10);text(scope,key,buffer);
}
void ArduinoPorts::number(const char* scope,const char* key,float value) {
    // 48 bytes also accommodate finite float extremes; the OEM's 28-byte
    // temporary is not deliberately reproduced as an overflow.
    char buffer[48];::dtostrf(static_cast<double>(value),4,2,buffer);text(scope,key,buffer);
}
void ArduinoPorts::boolean(const char* scope,const char* key,bool value) { text(scope,key,value?"True":"False"); }
}} // namespace picoferm::oem
