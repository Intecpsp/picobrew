// Original Z119 LIS3DHTR and BMP280 sensor services.
#pragma once
#include "zseries/sensor_acquisition.hpp"
#include <array>
#include <cstdint>

namespace zseries { namespace oem {
constexpr std::uint8_t OriginalLis3dhtrAddress = 0x19;
constexpr std::uint8_t OriginalBmp280Address = 0x76;

// Owned error reporting. Original setup/tick methods returned void and logged
// assertions. A failing register read still retains and processes prior raw
// values, as the original did; these bits make that result visible to Core.
enum AuxiliaryStatus : std::uint32_t {
    AuxiliaryOk = 0, AuxiliaryMissingService = 1, AuxiliaryTransmitFailure = 2,
    AuxiliaryShortReply = 4, AuxiliaryWrongId = 8, AuxiliaryZeroDivisor = 16
};

struct AuxiliarySensors {
    std::array<double, 3> acceleration{{0, 0, 0}};
    double pressure_pa = 0;
    std::uint32_t pressure_ms = 0;
};

struct Lis3dhtr {
    std::uint8_t address = OriginalLis3dhtrAddress;
    std::array<std::int16_t, 3> raw{{0, 0, 0}};
    bool polling_enabled = false;
};

struct Bmp280Calibration {
    std::uint16_t t1 = 0;
    std::int16_t t2 = 0, t3 = 0;
    std::uint16_t p1 = 0;
    std::int16_t p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;
};

struct Bmp280 {
    std::uint8_t address = OriginalBmp280Address;
    std::int64_t raw_pressure = 0, raw_temperature = 0;
    Bmp280Calibration calibration;
    std::int32_t temperature_fine = 0;
    bool polling_enabled = false;
};

// Original identical helpers 0x400d16b0 / 0x400d183c OR 0x80 into the
// selected register, end with STOP, and request the exact count with STOP.
// The original converts each read result to one byte, including -1 -> 255.
std::uint32_t read_auxiliary_register(std::uint8_t address, std::uint8_t reg,
                                     std::uint8_t* destination, std::uint8_t count,
                                     const I2cTransport&) noexcept;
std::uint32_t lis3dhtr_setup(Lis3dhtr&, const I2cTransport&) noexcept;
std::uint32_t lis3dhtr_tick(Lis3dhtr&, AuxiliarySensors&, const I2cTransport&) noexcept;
std::uint32_t bmp280_read_calibration(Bmp280&, const I2cTransport&) noexcept;
std::uint32_t bmp280_setup(Bmp280&, const I2cTransport&) noexcept;

// Original 0x400d91ac reuses byte[0] >> 4 as the low nibble, ignoring byte[2].
// This OEM defect is preserved; corrected firmware must change it explicitly.
std::uint32_t original_bmp280_raw(const std::uint8_t bytes[3]) noexcept;
double bmp280_compensate(Bmp280&, bool& zero_divisor) noexcept;
std::uint32_t bmp280_tick(Bmp280&, AuxiliarySensors&, HeaterSensors&,
                         const I2cTransport&, const AcquisitionClock&) noexcept;
}} // namespace zseries::oem
