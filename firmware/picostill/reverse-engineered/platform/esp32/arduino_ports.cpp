#include "arduino_ports.hpp"

namespace picostill { namespace oem {
std::uint32_t ArduinoPorts::millis() { return ::millis(); }
void ArduinoPorts::pin_mode(std::uint8_t pin, unsigned mode) { ::pinMode(pin,static_cast<std::uint8_t>(mode)); }
bool ArduinoPorts::digital_read(std::uint8_t pin) { return ::digitalRead(pin)!=0; }
void ArduinoPorts::digital_write(std::uint8_t pin, bool high) { ::digitalWrite(pin,high); }
void ArduinoPorts::pwm_setup(std::uint8_t channel, double frequency, unsigned resolution) { ::ledcSetup(channel,frequency,resolution); }
void ArduinoPorts::pwm_attach(std::uint8_t pin, std::uint8_t channel) { ::ledcAttachPin(pin,channel); }
void ArduinoPorts::pwm_write(std::uint8_t channel, std::uint8_t duty) { ::ledcWrite(channel,duty); }
void ArduinoPorts::begin_transmission(std::uint8_t address) { wire_.beginTransmission(address); }
void ArduinoPorts::write_byte(std::uint8_t value) { wire_.write(value); }
void ArduinoPorts::end_transmission() { wire_.endTransmission(true); }
unsigned ArduinoPorts::request(std::uint8_t address, unsigned size) {
    return wire_.requestFrom(static_cast<std::uint16_t>(address),static_cast<std::uint8_t>(size),true);
}
int ArduinoPorts::available() { return wire_.available(); }
int ArduinoPorts::read_byte() { return wire_.read(); }
void ArduinoPorts::delay_ms(std::uint32_t duration) { ::delay(duration); }
void ArduinoPorts::assertion(const char* file, unsigned line, const char* text) {
    const float seconds=static_cast<float>(::millis())/1000.0f;
    serial_.printf("\033[30;41m[E_][%8.2f][ASSERT][%s:%u] %s\033[0m\r\n",static_cast<double>(seconds),file,line,text);
}
void ArduinoPorts::message(const char* text) { serial_.println(text); ::yield(); }
void ArduinoPorts::variable(const char* scope, const char* name, const char* value) {
    // Formatting/output is an explicit reconstruction boundary. No tokens,
    // credential configuration or network listeners are introduced here.
    serial_.printf("%4.2f VAR [%s] %s=%s\r\n",static_cast<double>(static_cast<float>(::millis())/1000.0f),scope,name,value);
    ::yield();
}
bool ArduinoPorts::initialize_hardware(FanBank& fans, OutputPin& pump, Sensors& sensors) {
    pin_mode(fans.power.pin,2);digital_write(fans.power.pin,false);
    fans.first.setup();fans.second.setup();
    pin_mode(pump.pin,2);digital_write(pump.pin,false);
    const bool ready=wire_.begin(14,27,100000);
    begin_transmission(sensors.adc.address);write_byte(0x80);delay_ms(10);
    // Preserve OEM ordering: it starts a second transmission without ending
    // the first. Changing that behavior requires separate modified evidence.
    begin_transmission(sensors.adc.address);write_byte(0x82);write_byte(7);end_transmission();
    sensors.adc.three_channels=false;sensors.adc.clear();delay_ms(200);
    sensors.sample_pressure();sensors.sample_temperatures();
    return ready;
}
}} // namespace picostill::oem
