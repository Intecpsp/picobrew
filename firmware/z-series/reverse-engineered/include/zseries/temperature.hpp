// Readable reconstruction of the Z 0.0.119 MAX11613 temperature calculations.
// Evidence and the boundary of this reconstruction: analysis/temperature.md.
#pragma once

#include "zseries/enums.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {

struct ThermistorParameters {
    TempSensorType type;
    double a;
    double b;
    double c;
    double series_resistance_ohms;
};

// The original table has four 40-byte records at DROM 0x3f4287f0. This
// accessor owns its constants; the reconstructed code needs no OEM image.
// A null result is an adapter-level rejection of an invalid enum value.
const ThermistorParameters* thermistor_parameters(TempSensorType type) noexcept;

constexpr std::int32_t MinimumRawAdc = 15;
constexpr std::int32_t MaximumRawAdc = 4094;
constexpr std::int32_t InvalidRawAdc = -1;

bool raw_adc_valid(std::int32_t raw) noexcept;

// Reconstructed function 0x400d6ac0. Raw samples are clamped to 15..4094,
// matching the original calculation even when its caller has not checked
// validity. Diagnostic formatting is outside this pure calculation.
double thermistor_kelvin(const ThermistorParameters& parameters,
                         std::int32_t raw) noexcept;

// The subtraction in original wrapper 0x400dd770. The original wrapper also
// asserts raw/type validity; callers can use raw_adc_valid before conversion.
double thermistor_celsius(const ThermistorParameters& parameters,
                          std::int32_t raw) noexcept;

using AdcChannels = std::array<std::int32_t, TempSensorIdCount>;

// Pure byte portion of 0x400dd908, in Hex/Wort/Drain/Ambient order. Failure
// resets all channels to -1. The I2C request, byte reads and assertions remain
// a separate transport boundary; this is not a hardware driver.
bool decode_max11613_reply(const std::uint8_t* bytes, std::size_t size,
                          AdcChannels& channels) noexcept;

// Valid-domain behavior of 0x400dd708. Invalid boiler values are rejected
// rather than reproducing the OEM assert-and-fallback path.
bool hex_sensor_for_boiler(BoilerType boiler, TempSensorType& sensor) noexcept;

}} // namespace zseries::oem
