// Reconstructed from the preserved Z 0.0.119 image; not original vendor source.
#include "zseries/temperature.hpp"
#include <cmath>

namespace zseries { namespace oem {
namespace {

// Exact IEEE-754 binary64 values from DROM 0x3f4287f0. Decimal spellings
// round-trip to the original bits; tests verify every coefficient against
// the preserved image, independently of this source.
const ThermistorParameters Parameters[] = {
    {TempSensorType::SENSTECH_10_366,   0.0009111439983, 0.0002472353473, 2.121428624e-7, 10000.0},
    {TempSensorType::SENSTECH_10_364_5, 0.001127286052,  0.0002351430103, 7.753973551e-8, 10000.0},
    {TempSensorType::MURATA_NCU18,     0.000901747748,  0.000248919031,  2.043213857e-7, 10000.0},
    {TempSensorType::AET_14015,        0.0006232719375, 0.0002276660423, 7.184906863e-8, 10000.0},
};

} // namespace

const ThermistorParameters* thermistor_parameters(TempSensorType type) noexcept {
    const auto index = static_cast<std::uint32_t>(type);
    return index < TempSensorTypeCount ? &Parameters[index] : nullptr;
}

bool raw_adc_valid(std::int32_t raw) noexcept {
    // The subtraction is unsigned in the OEM instruction sequence; cast
    // first so even INT32_MIN has defined behavior in the reconstruction.
    return static_cast<std::uint32_t>(raw) - MinimumRawAdc <=
           static_cast<std::uint32_t>(MaximumRawAdc - MinimumRawAdc);
}

double thermistor_kelvin(const ThermistorParameters& parameters,
                         std::int32_t raw) noexcept {
    if (raw < MinimumRawAdc) raw = MinimumRawAdc;
    else if (raw > MaximumRawAdc) raw = MaximumRawAdc;

    const double resistance = (4095.0 / static_cast<double>(raw) - 1.0) *
                              parameters.series_resistance_ohms;
    const double log_resistance = std::log(resistance);
    // Preserve the original multiply/add order, including the cubic term.
    // Builds used for numerical equivalence must disable fast-math and FMA
    // contraction. No sensor-specific polynomial refit is performed here.
    const double linear = log_resistance * parameters.b + parameters.a;
    const double cubic = ((log_resistance * parameters.c) * log_resistance) *
                         log_resistance;
    return 1.0 / (linear + cubic);
}

double thermistor_celsius(const ThermistorParameters& parameters,
                          std::int32_t raw) noexcept {
    return thermistor_kelvin(parameters, raw) - 273.15;
}

bool decode_max11613_reply(const std::uint8_t* bytes, std::size_t size,
                          AdcChannels& channels) noexcept {
    channels.fill(InvalidRawAdc);
    if (size != 8 || bytes == nullptr) return false;
    for (std::size_t i = 0; i < channels.size(); ++i) {
        channels[i] = ((static_cast<std::uint32_t>(bytes[i * 2]) << 8) |
                       bytes[i * 2 + 1]) & 0x0fffU;
    }
    return true;
}

bool hex_sensor_for_boiler(BoilerType boiler, TempSensorType& sensor) noexcept {
    switch (boiler) {
        case BoilerType::Big: sensor = TempSensorType::SENSTECH_10_366; return true;
        case BoilerType::Small: sensor = TempSensorType::AET_14015; return true;
        default: return false;
    }
}

}} // namespace zseries::oem
