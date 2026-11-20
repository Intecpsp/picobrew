// PicoStill board calibration and original I2C sensor acquisition.
#pragma once
#include "picostill/core.hpp"
#include <array>
#include <cstdint>

namespace picostill { namespace oem {
// Channel order is In, Out, Pot, Ambient at the original four call sites.
const ThermistorParameters* board_thermistor(unsigned channel) noexcept;

struct I2cPorts {
    virtual ~I2cPorts() = default;
    virtual void begin_transmission(std::uint8_t address) = 0;
    virtual void write_byte(std::uint8_t value) = 0;
    virtual void end_transmission() = 0;
    virtual unsigned request(std::uint8_t address, unsigned size) = 0;
    virtual int available() = 0;
    virtual int read_byte() = 0;
    virtual void delay_ms(std::uint32_t duration) = 0;
};
struct AdcReader {
    std::uint8_t address = 0x34;
    bool three_channels = false;
    std::array<std::uint16_t,4> raw = {{0,0,0,0}};
    std::array<bool,4> valid = {{false,false,false,false}};
    void clear() noexcept;
    void read(I2cPorts& ports);
};
struct PressureReader {
    std::uint8_t expected_status = 0x40, address = 0x18, range = 25;
    double value = 0;
    bool valid = false;
    void read(I2cPorts& ports);
};
class Sensors {
public:
    Sensors(I2cPorts& ports, Diagnostics& diagnostics) noexcept : ports_(ports), diagnostics_(diagnostics) {}
    AdcReader adc;
    PressureReader pressure;
    std::array<float,5> values = {{0,0,0,0,0}};
    SensorMetrics metrics;
    void sample_temperatures();
    void sample_pressure();
    void reset_metrics() noexcept;
private:
    I2cPorts& ports_;
    Diagnostics& diagnostics_;
};
}} // namespace picostill::oem
