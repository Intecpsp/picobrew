#pragma once
#include "picoferm/hardware.hpp"
#include <Wire.h>
namespace picoferm { namespace oem {
class ArduinoHardwarePorts final : public HardwarePorts {
public:
    explicit ArduinoHardwarePorts(TwoWire& wire) noexcept:wire_(wire){}
    std::uint32_t millis() override;
    void pin_mode(unsigned pin,unsigned mode) override;
    std::int32_t analog_read(unsigned pin) override;
    void wire_begin(unsigned sda,unsigned scl) override;
    void wire_begin_transmission(unsigned address) override;
    void wire_write(std::uint8_t value) override;
    void wire_end_transmission() override;
    void wire_request(unsigned address,unsigned count) override;
    std::uint32_t wire_read() override;
private:TwoWire& wire_;
};
}}
