// Concrete ESP32 Arduino driver for the recovered PicoStill modules.
#pragma once
#include "picostill/hardware.hpp"
#include "picostill/sensors.hpp"
#include <Arduino.h>
#include <Wire.h>

namespace picostill { namespace oem {
class ArduinoPorts final : public DiagnosticPorts, public HardwarePorts, public I2cPorts {
public:
    explicit ArduinoPorts(HardwareSerial& serial=Serial, TwoWire& wire=Wire) noexcept
        : serial_(serial), wire_(wire) {}
    std::uint32_t millis() override;
    void pin_mode(std::uint8_t pin, unsigned mode) override;
    bool digital_read(std::uint8_t pin) override;
    void digital_write(std::uint8_t pin, bool high) override;
    void pwm_setup(std::uint8_t channel, double frequency, unsigned resolution) override;
    void pwm_attach(std::uint8_t pin, std::uint8_t channel) override;
    void pwm_write(std::uint8_t channel, std::uint8_t duty) override;
    void begin_transmission(std::uint8_t address) override;
    void write_byte(std::uint8_t value) override;
    void end_transmission() override;
    unsigned request(std::uint8_t address, unsigned size) override;
    int available() override;
    int read_byte() override;
    void delay_ms(std::uint32_t duration) override;
    void assertion(const char* file, unsigned line, const char* text) override;
    void message(const char* text) override;
    void variable(const char* scope, const char* name, const char* value) override;
    // Original 0x400d7474 setup order. Constructs no application, listener or
    // task. All supplied modules must use this same driver instance.
    bool initialize_hardware(FanBank& fans, OutputPin& pump, Sensors& sensors);
private:
    HardwareSerial& serial_;
    TwoWire& wire_;
};
}} // namespace picostill::oem
